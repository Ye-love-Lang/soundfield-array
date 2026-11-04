#include "spatial_control.h"
#include "spatial_design.h"
#include "adaptive.h"
#include <string.h>
enum {OFF,IDLE,DESIGN,BLEND,UPLOAD,WAIT_COMMIT,HOLD,ERROR};
static SFRead rd;static SFWrite wr;static SFControlStatus status;
static int32_t current[SF_COEFFICIENTS],origin[SF_COEFFICIENTS],stage_coeff[SF_COEFFICIENTS];
static unsigned job_target,blend_index,upload_index,step,steps,old_bank;
static int job_interference,selected_interference;
static uint32_t selected_id,candidate_id,last_seen,observed_at,last_sequence,job_at,wait_at,hold_at,fault_at,errors_at_init,overruns_at_init;
static unsigned candidate_angle,candidate_hits;static int applied;
static unsigned design_epoch,applied_epoch;
static unsigned distance(unsigned a,unsigned b){unsigned d=(a+360-b)%360;return d>180?360-d:d;}
static int round_direction(unsigned angle){return(int)(((angle+2)/5*5)%360);}
static void fail(uint32_t now){status.failures++;status.phase=ERROR;fault_at=now-10;}
static void reject_design(uint32_t now,unsigned reason){
 status.rejected_designs++;status.last_design_error=reason;
 /* No upload has begun. Keep the committed, valid bank sounding; hardware
  * counter/commit errors still take the existing hard-fault path. */
 if(!applied){fail(now);return;}
 status.phase=IDLE;job_at=now;
}
int sf_control_init(SFRead read_fn,SFWrite write_fn){
 if(!read_fn||!write_fn)return 0;
 rd=read_fn;wr=write_fn;memset(&status,0,sizeof status);memset(current,0,sizeof current);
 selected_interference=-1;selected_id=candidate_id=0;candidate_hits=0;applied=0;
 design_epoch=applied_epoch=0;
 observed_at=last_sequence=last_seen=job_at=wait_at=hold_at=fault_at=0;
 status.applied_target=360;status.applied_interference=360;
 status.supported=rd(0xA0)==0x46495231U;
 if(!status.supported)return 0;
 errors_at_init=rd(0xA8);overruns_at_init=rd(0xA4);
 if(rd(0x9C)&1U){fail(0);return 1;} /* Poll fault path to disable unknown coefficients after PS-only restart. */
 status.phase=IDLE;return 1;
}
static void observe(uint32_t now,unsigned target,uint32_t seq,int fresh,const SFSource *s,unsigned count){
 if(selected_interference>=0&&distance(target,(unsigned)selected_interference)<45){selected_interference=-1;selected_id=0;candidate_hits=0;}
 if(selected_interference>=0&&(uint32_t)(now-last_seen)>1200){selected_interference=-1;selected_id=0;}
 if(seq==last_sequence||(uint32_t)(now-observed_at)<100)return;
 last_sequence=seq;observed_at=now;
 const SFSource *best=0;
 if(fresh&&s)for(unsigned i=0;i<count&&i<4;i++){
  if(!s[i].id||s[i].angle>359||s[i].strength<200||distance(target,s[i].angle)<45)continue;
  if(s[i].id==selected_id)last_seen=now;
  if(!best||s[i].strength>best->strength)best=&s[i];
 }
 if(best){
  if(best->id==candidate_id&&distance(best->angle,candidate_angle)<=10)candidate_hits++;else candidate_hits=1;
  candidate_id=best->id;candidate_angle=best->angle;
  if(candidate_hits>=3){selected_interference=round_direction(best->angle);selected_id=best->id;last_seen=now;candidate_hits=3;}
 }else{candidate_id=0;candidate_hits=0;}
}
static void start_design(uint32_t now,unsigned target){
 job_target=target;job_interference=selected_interference;job_at=now;
 design_epoch=ad_freeze();
 memcpy(origin,current,sizeof origin);
 if(!sf_design_begin(target,job_interference,60)){fail(now);return;}
 status.phase=DESIGN;step=1;steps=applied?8:1;
}
void sf_control_poll(uint32_t now,unsigned target,uint32_t sequence,int fresh,const SFSource *sources,unsigned count){
 if(!status.supported||target>359)return;
 uint32_t hw=rd(0x9C);
 if(status.phase==ERROR){
  if((hw&1U)&&!(hw&4U)&&(uint32_t)(now-fault_at)>=10){wr(0x98,2);fault_at=now;}
  return;
 }
 if(rd(0xA8)!=errors_at_init||rd(0xA4)!=overruns_at_init||(applied&&!(hw&1U))){fail(now);return;}
 observe(now,target,sequence,fresh,sources,count);
 if(status.phase==IDLE){
  int changed=!applied||target!=status.applied_target||(unsigned)(selected_interference<0?360:selected_interference)!=status.applied_interference||((uint32_t)(now-job_at)>=2000&&ad_epoch()!=applied_epoch);
  if(changed&&(!applied||target!=status.applied_target||(uint32_t)(now-job_at)>=500))start_design(now,target);
 }else if(status.phase==DESIGN){
  if(target!=job_target){start_design(now,target);return;}
  int result=sf_design_step();if(result<0){reject_design(now,sf_design_error());return;}
  if(result){blend_index=0;status.phase=BLEND;}
 }else if(status.phase==BLEND){
  for(unsigned n=0;n<32&&blend_index<SF_COEFFICIENTS;n++,blend_index++){
   int64_t value=(int64_t)origin[blend_index]*(steps-step)+(int64_t)sf_design_coefficient(blend_index)*step;
   stage_coeff[blend_index]=(int32_t)(value>=0?(value+steps/2)/steps:-((-value+steps/2)/steps));
  }
  if(blend_index==SF_COEFFICIENTS){
   for(unsigned c=0;c<8;c++){int64_t sum=0;for(unsigned k=0;k<512;k++)sum+=stage_coeff[c*512+k];stage_coeff[c*512+256]-=(int32_t)sum;}
   for(unsigned i=0;i<SF_COEFFICIENTS;i++)if(stage_coeff[i]< -524288||stage_coeff[i]>524287){reject_design(now,5);return;}
   wr(0x90,0);upload_index=0;status.phase=UPLOAD;
  }
 }else if(status.phase==UPLOAD){
  for(unsigned n=0;n<32&&upload_index<SF_COEFFICIENTS;n++,upload_index++)wr(0x94,(uint32_t)stage_coeff[upload_index]);
  if(upload_index==SF_COEFFICIENTS){
   hw=rd(0x9C);if(!(hw&4U)){fail(now);return;}
   old_bank=(hw>>1)&1U;wr(0x98,3);wait_at=now;status.phase=WAIT_COMMIT;
  }
 }else if(status.phase==WAIT_COMMIT){
  if((hw&1U)&&((hw>>1)&1U)!=old_bank&&!(hw&4U)){
   memcpy(current,stage_coeff,sizeof current);applied=1;hold_at=now;
   if(step==steps){status.applied_target=job_target;status.applied_interference=(unsigned)(job_interference<0?360:job_interference);status.updates++;applied_epoch=design_epoch;status.phase=IDLE;}
   else status.phase=HOLD;
  }else if((uint32_t)(now-wait_at)>100)fail(now);
 }else if(status.phase==HOLD&&(uint32_t)(now-hold_at)>=10){step++;blend_index=0;status.phase=BLEND;}
}
void sf_control_status(SFControlStatus *out){if(out)*out=status;}
void sf_control_timing(uint32_t ms){if(ms>status.max_poll_ms)status.max_poll_ms=ms;}
