#include "raw8.h"
#include "adaptive.h"
#include "xil_io.h"
#include "uptime.h"
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846
static Raw8Status status;
static int32_t record[8192],exported[8192];
static AdComplex spectrum[8][512];
static unsigned cursor,channel,block,bin,export_pending;
static int cal_pending=-1;
static unsigned cal_path_pending;
static uint32_t started,hw_overruns,hw_errors;
static uint32_t rd(unsigned x){return Xil_In32((UINTPTR)0x43C00000U+x);}
static void wr(unsigned x,uint32_t v){Xil_Out32((UINTPTR)0x43C00000U+x,v);}
static void fft(AdComplex a[512]){
 for(unsigned i=1,j=0;i<512;i++){unsigned bit=256;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j){AdComplex t=a[i];a[i]=a[j];a[j]=t;}}
 for(unsigned length=2;length<=512;length*=2){double p=-2*PI/length;AdComplex step={cos(p),sin(p)};
  for(unsigned start=0;start<512;start+=length){AdComplex w={1,0};
   for(unsigned i=0;i<length/2;i++){AdComplex b=a[start+i+length/2];AdComplex v={b.r*w.r-b.i*w.i,b.r*w.i+b.i*w.r},u=a[start+i];a[start+i]=(AdComplex){u.r+v.r,u.i+v.i};a[start+i+length/2]=(AdComplex){u.r-v.r,u.i-v.i};double r=w.r*step.r-w.i*step.i;w.i=w.i*step.r+w.r*step.i;w.r=r;}
  }
 }
}
void raw8_init(void){memset(&status,0,sizeof status);status.supported=rd(0xB0)==0x52415733U;export_pending=0;ad_init();}
int raw8_export_arm(void){if(!status.supported||export_pending)return 0;export_pending=1;status.export_ready=0;return 1;}
int raw8_calibrate(unsigned angle){AdStatus s;ad_status(&s);if(!status.supported||angle>359||cal_pending>=0||s.cal_state==1)return 0;cal_pending=(int)angle;cal_path_pending=0;return 1;}
int raw8_calibrate_path(unsigned angle){if(!raw8_calibrate(angle))return 0;cal_path_pending=1;return 1;}
int raw8_read(unsigned offset,unsigned count,int32_t *out){if(!status.export_ready||!out||offset>8192||count>8192-offset)return 0;memcpy(out,exported+offset,count*sizeof *out);return 1;}
void raw8_status(Raw8Status *out){if(out)*out=status;}
void raw8_poll(void){
 if(!status.supported)return;
 uint32_t now=uptime_millis(),before=now;
 if(status.state==0){if(cal_pending>=0){if(cal_path_pending)ad_path_calibrate((unsigned)cal_pending);else ad_calibrate((unsigned)cal_pending);cal_pending=-1;}hw_overruns=rd(0xD0);hw_errors=rd(0xD8);wr(0xC0,1);started=now;status.state=1;}
 else if(status.state==1){
  if(rd(0xB4)&2U){
   status.generation=rd(0xC4);status.first=rd(0xC8);status.last=rd(0xCC);status.clips=rd(0xD4);
   if(status.last-status.first!=1023U||rd(0xD0)!=hw_overruns||rd(0xD8)!=hw_errors){status.errors++;status.state=0;}
   else{cursor=0;wr(0xB8,0);status.state=2;}
  }else if(now-started>250){status.errors++;status.state=0;}
 }else if(status.state==2){
  for(unsigned i=0;i<32&&cursor<8192;i++)record[cursor++]=(int32_t)rd(0xBC);
  if(cursor==8192){
   status.records++;
   if(export_pending){memcpy(exported,record,sizeof record);status.export_generation=status.generation;status.export_first=status.first;status.export_last=status.last;status.export_clips=status.clips;status.export_ready=1;export_pending=0;}
   if(status.clips){status.errors++;status.state=0;}
   else{block=channel=0;status.state=3;}
  }
 }else if(status.state==3){
  double mean=0;for(unsigned i=0;i<512;i++)mean+=record[((block*512+i)*8)+channel]/256.0;mean/=512;
  for(unsigned i=0;i<512;i++){double x=record[((block*512+i)*8)+channel]/256.0-mean;spectrum[channel][i]=(AdComplex){x*(.5-.5*cos(2*PI*i/511)),0};}
  fft(spectrum[channel]);
  if(++channel==8){bin=0;status.state=4;}
 }else if(status.state==4){
  AdComplex x[8];for(unsigned i=0;i<8;i++)x[i]=(AdComplex){spectrum[i][bin].r/256,spectrum[i][bin].i/256};
  ad_observe(bin,x);
  if(++bin==257){ad_frame_done();if(++block==2)status.state=0;else{channel=0;status.state=3;}}
 }
 uint32_t elapsed=uptime_millis()-before;if(elapsed>status.max_poll_ms)status.max_poll_ms=elapsed;
}
