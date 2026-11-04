#include "spatial_control.h"
#include "spatial_design.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int supported=1,enabled,bank,ready,pending,freeze;
static unsigned index_reg,loaded,errors,overruns,commits,writes,data_writes,max_data;
static int32_t inactive[4096],active[4096];static uint32_t now;
static int32_t commit_history[9][4096];
static uint32_t read_reg(unsigned r){
 switch(r){case 0xA0:return supported?0x46495231:0;case 0x9C:return enabled|(bank<<1)|(ready<<2);case 0xA8:return errors;case 0xA4:return overruns;default:return 0;}
}
static void write_reg(unsigned r,uint32_t value){
 writes++;
 if(r==0x90){CHECK(!pending);CHECK(value==0);index_reg=loaded=ready=0;}
 else if(r==0x94){
  CHECK(!pending&&index_reg==loaded&&index_reg<4096);data_writes++;
  inactive[index_reg++]=(value&0x80000)?(int32_t)(value|0xfff00000):(int32_t)(value&0xfffff);
  loaded++;if(loaded==4096){ready=1;index_reg=0;}
 }else if(r==0x98){
  if(value==3){CHECK(ready&&!pending);pending=1;}
  else if(value==2){CHECK(!pending);pending=2;}else CHECK(0);
 }else CHECK(0);
}
static void tick(unsigned target,const SFSource *sources,unsigned count,int fresh,int stale){
 now++;
 if(pending&&!freeze){if(pending==1){memcpy(active,inactive,sizeof active);bank^=1;enabled=1;ready=0;loaded=0;commits++;if(commits<=9)memcpy(commit_history[commits-1],active,sizeof active);for(unsigned c=0;c<8;c++){int64_t sum=0;for(unsigned k=0;k<512;k++)sum+=active[c*512+k];CHECK(sum==0);}}else enabled=0;pending=0;}
 data_writes=0;sf_control_poll(now,target,stale?1:now/25,fresh,sources,count);
 CHECK(data_writes<=32);if(data_writes>max_data)max_data=data_writes;
}
static SFControlStatus get(void){SFControlStatus s;sf_control_status(&s);return s;}
static void until_update(unsigned n,unsigned target,const SFSource *s,unsigned count){
 unsigned limit=now+6000;while(get().updates<n&&now<limit)tick(target,s,count,1,0);CHECK(get().updates>=n&&get().phase==1);
}
static void reset(void){enabled=bank=ready=pending=freeze=0;index_reg=loaded=errors=overruns=commits=writes=now=0;memset(active,0,sizeof active);CHECK(sf_control_init(read_reg,write_reg));}
int main(void){
 supported=0;CHECK(!sf_control_init(read_reg,write_reg));tick(90,0,0,0,0);CHECK(writes==0);supported=1;reset();
 until_update(1,90,0,0);CHECK(enabled&&commits==1&&get().applied_target==90&&get().applied_interference==360);
 SFSource source={7,180,650};unsigned before=commits;
 for(unsigned i=0;i<150;i++)tick(90,&source,1,1,0);CHECK(commits==before);
 until_update(2,90,&source,1);CHECK(commits==before+8&&get().applied_interference==180);
 for(unsigned stage=1;stage<=8;stage++)for(unsigned c=0;c<8;c++){
  int32_t expected[512];int64_t sum=0;
  for(unsigned k=0;k<512;k++){
   unsigned i=c*512+k;
   double value=((double)commit_history[0][i]*(8-stage)+(double)commit_history[8][i]*stage)/8;
   expected[k]=(int32_t)round(value);sum+=expected[k];
  }
  expected[256]-=(int32_t)sum;
  for(unsigned k=0;k<512;k++)CHECK(commit_history[stage][c*512+k]==expected[k]);
 }
 for(unsigned i=0;i<4096;i++)CHECK(active[i]==sf_design_coefficient(i));
 unsigned settled=get().updates;
 source.angle=95;until_update(settled+1,90,&source,1);CHECK(get().applied_interference==360);
 for(unsigned i=0;i<20;i++)tick(45,0,0,0,0);
 until_update(get().updates+1,125,0,0);CHECK(get().applied_target==125);
 settled=get().updates;source.angle=270;
 for(unsigned i=0;i<800;i++){source.id=1+(now/100)%2;tick(125,&source,1,1,0);}CHECK(get().updates==settled);
 source.id=20;source.angle=270;until_update(settled+1,125,&source,1);CHECK(get().applied_interference==270);
 settled=get().updates;unsigned limit=now+6000;
 while(get().updates==settled&&now<limit)tick(125,0,0,0,1);CHECK(get().updates>settled&&get().applied_interference==360);
 errors++;tick(125,0,0,0,0);CHECK(get().phase==7&&get().failures==1);
 for(unsigned i=0;i<30;i++)tick(125,0,0,0,0);CHECK(!enabled);
 reset();freeze=1;limit=now+2000;while(get().phase!=7&&now<limit)tick(90,0,0,0,0);CHECK(get().phase==7&&get().failures==1);
 freeze=0;for(unsigned i=0;i<30;i++)tick(90,0,0,0,0);CHECK(!enabled);
 sf_control_timing(3);sf_control_timing(1);CHECK(get().max_poll_ms==3);
 printf("{\"passed\":true,\"max_data_writes_per_poll\":%u,\"fade_commits_per_update\":8,\"dc_sum_zero\":true,\"tests\":\"legacy,new source confirmation,protected sector,retarget,unstable IDs,stale sequence release,fault,commit timeout\"}\n",max_data);
 return 0;
}
