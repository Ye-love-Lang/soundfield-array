#include "audio_capture.h"
#include <string.h>
static int16_t samples[AC_SAMPLES];
static ACContext current;
static ACStatus status;
static unsigned discard;
static int completion_check;
void ac_context(const ACContext *context){
 current=*context;
 if((status.state==1U || completion_check) && memcmp(&current,&status.context,sizeof current)!=0)
  status.valid=0U;
 if(status.state==2U)completion_check=0;
}
int ac_arm(void){
 if(status.state==1U || !current.active || current.phase!=1U || current.failures)
  return 0;
 uint32_t generation=status.generation+1U;
 memset(&status,0,sizeof status);
 status.generation=generation;status.state=1;status.valid=1;
 status.context=current;discard=AC_DISCARD;completion_check=0;return 1;
}
int ac_running(void){return status.state==1U || completion_check;}
void ac_push(int16_t sample){
 if(status.state!=1U)return;
 if(discard){--discard;return;}
 int32_t x=sample;uint32_t magnitude=(uint32_t)(x<0?-x:x);
 samples[status.count++]=sample;
 status.sum+=x;status.squares+=(uint64_t)((int64_t)x*x);
 if(magnitude>status.peak)status.peak=magnitude;
 if(x==-32768 || x==32767)++status.rails;
 if(status.count==AC_SAMPLES){status.state=2U;completion_check=1;}
}
void ac_status(ACStatus *out){if(out)*out=status;}
int ac_read(unsigned offset,unsigned count,int16_t *out){
 if(status.state!=2U || !out || offset>AC_SAMPLES || count>AC_SAMPLES-offset)
  return 0;
 memcpy(out,samples+offset,count*sizeof *out);return 1;
}
