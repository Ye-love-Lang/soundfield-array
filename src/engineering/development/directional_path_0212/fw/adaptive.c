#include "adaptive.h"
#include <math.h>
#include <string.h>
#define BINS 257
#define PI 3.14159265358979323846
typedef AdComplex C;
static C live[BINS][8][8],frozen[BINS][8][8],gain[BINS][8],frozen_gain[BINS][8];
static AdStatus status;
static unsigned frozen_frames,cal_frames;
static AdCalBin cal_diagnostics[52];
static C cal_proposed[52][8];
static C path_gain[BINS][8],frozen_path[BINS][8],path_candidate[52][8];
static unsigned char path_mask[BINS],frozen_path_mask[BINS],candidate_mask[52];
static unsigned path_angle,frozen_path_angle;
static C add(C a,C b){return(C){a.r+b.r,a.i+b.i};}
static C sub(C a,C b){return(C){a.r-b.r,a.i-b.i};}
static C mul(C a,C b){return(C){a.r*b.r-a.i*b.i,a.r*b.i+a.i*b.r};}
static C sc(C a,double b){return(C){a.r*b,a.i*b};}
static C cj(C a){return(C){a.r,-a.i};}
static double mag(C a){return a.r*a.r+a.i*a.i;}
static C divc(C a,C b){return sc(mul(a,cj(b)),1/fmax(1e-30,mag(b)));}
static C dot(const C a[8],const C b[8]){C s={0,0};for(unsigned i=0;i<8;i++)s=add(s,mul(cj(a[i]),b[i]));return s;}
static void steer(unsigned k,double angle,C a[8]){
 for(unsigned i=0;i<8;i++){double p=2*PI*(k*93.75)*60/343000*cos((90+45*i-angle)*PI/180);a[i]=mul(frozen_gain[k][i],(C){cos(p),sin(p)});}
 /* A measured room path is local to its measured direction. Never apply it
  * as a hardware correction to all other directions. */
 if(frozen_path_mask[k]&&fabs(remainder(angle-frozen_path_angle,360.0))<=15){
  for(unsigned i=0;i<8;i++){
   double p=2*PI*k*93.75*60/343000*(cos((90+45.0*i-angle)*PI/180)-cos((90+45.0*i-frozen_path_angle)*PI/180));
   a[i]=mul(frozen_path[k][i],(C){cos(p),sin(p)});
  }
 }
}
static int solve(C A[8][8],const C b[8],C x[8]){
 C m[8][9];for(unsigned i=0;i<8;i++){for(unsigned j=0;j<8;j++)m[i][j]=A[i][j];m[i][8]=b[i];}
 for(unsigned k=0;k<8;k++){
  unsigned p=k;for(unsigned i=k+1;i<8;i++)if(mag(m[i][k])>mag(m[p][k]))p=i;
  if(mag(m[p][k])<1e-24)return 0;
  if(p!=k)for(unsigned j=k;j<9;j++){C t=m[k][j];m[k][j]=m[p][j];m[p][j]=t;}
  C d=m[k][k];for(unsigned j=k;j<9;j++)m[k][j]=divc(m[k][j],d);
  for(unsigned i=0;i<8;i++)if(i!=k){C q=m[i][k];for(unsigned j=k;j<9;j++)m[i][j]=sub(m[i][j],mul(q,m[k][j]));}
 }
 for(unsigned i=0;i<8;i++)x[i]=m[i][8];
 return 1;
}
void ad_init(void){
 memset(&status,0,sizeof status);memset(live,0,sizeof live);memset(cal_diagnostics,0,sizeof cal_diagnostics);status.enabled=1;
 for(unsigned k=0;k<BINS;k++)for(unsigned i=0;i<8;i++)gain[k][i]=frozen_gain[k][i]=(C){1,0};
 frozen_frames=cal_frames=0;
 memset(path_mask,0,sizeof path_mask);memset(frozen_path_mask,0,sizeof frozen_path_mask);path_angle=frozen_path_angle=0;
}
void ad_observe(unsigned k,const C x[8]){
 if(k>=BINS)return;
 for(unsigned i=0;i<8;i++)if(!isfinite(x[i].r)||!isfinite(x[i].i))return;
 double alpha=status.frames?0.08:1;
 for(unsigned i=0;i<8;i++)for(unsigned j=0;j<8;j++)
  live[k][i][j]=add(sc(live[k][i][j],1-alpha),sc(mul(x[i],cj(x[j])),alpha));
}
int ad_calibrate(unsigned angle){
 if(angle>359||status.cal_state==1)return 0;
 status.cal_angle=angle;status.cal_state=1;status.cal_bins=0;cal_frames=0;
 status.cal_mode=0;status.cal_stage=1;
 status.cal_power_bins=status.cal_coherent_bins=status.cal_amplitude_bins=status.cal_phase_bins=0;
 memset(cal_diagnostics,0,sizeof cal_diagnostics);
 status.frames=0;memset(live,0,sizeof live);return 1;
}
int ad_path_calibrate(unsigned angle){
 if(!ad_calibrate(angle))return 0;
 status.cal_mode=1;memset(candidate_mask,0,sizeof candidate_mask);return 1;
}
static int path_estimate(unsigned k,C q[8],AdCalBin *d){
 double p0=live[k][0][0].r;unsigned flags=7;
 d->min_coherence=d->min_amplitude=1e30;d->max_amplitude=d->max_phase_deg=0;
 for(unsigned i=0;i<8;i++){
  double power=live[k][i][i].r,coh=mag(live[k][i][0])/fmax(1e-30,p0*power);
  q[i]=sc(live[k][i][0],1/fmax(1e-30,p0));double amp=sqrt(mag(q[i]));
  if(!isfinite(power)||!isfinite(p0)||power<1e-4||p0<1e-4)flags&=~1U;
  if(!isfinite(coh)||coh<.90)flags&=~2U;
  if(!isfinite(amp)||amp<.125||amp>8)flags&=~4U;
  d->min_coherence=fmin(d->min_coherence,coh);d->min_amplitude=fmin(d->min_amplitude,amp);d->max_amplitude=fmax(d->max_amplitude,amp);
 }
 d->flags=flags;return flags==7;
}
static void path_finish_stage(void){
 unsigned good=0,first=0,last=0;status.cal_power_bins=status.cal_coherent_bins=status.cal_amplitude_bins=status.cal_phase_bins=0;
 for(unsigned k=2;k<=53;k++){
  C q[8];AdCalBin d;int valid=path_estimate(k,q,&d);
  if(d.flags&1)status.cal_power_bins++;
  if(d.flags&2)status.cal_coherent_bins++;
  if(d.flags&4)status.cal_amplitude_bins++;
  if(status.cal_stage==1){candidate_mask[k-2]=(unsigned char)valid;memcpy(path_candidate[k-2],q,sizeof q);}
  else {
   valid=valid&&candidate_mask[k-2];double worst=0;
   for(unsigned i=0;i<8;i++){
    C ratio=divc(q[i],path_candidate[k-2][i]);double amp=sqrt(mag(ratio)),phase=fabs(atan2(ratio.i,ratio.r))*180/PI;
    if(!isfinite(amp)||!isfinite(phase)||amp<.8||amp>1.25||phase>15)valid=0;
    worst=fmax(worst,phase);
   }
   d.max_phase_deg=worst;if(valid)d.flags|=8;candidate_mask[k-2]=(unsigned char)valid;
  }
  cal_diagnostics[k-2]=d;
  if(valid){good++;if(!first)first=k;last=k;}
 }
 status.cal_bins=good;cal_frames=0;status.frames=0;memset(live,0,sizeof live);status.epoch++;
 if(status.cal_stage==1){status.cal_stage=2;return;}
 /* Separate held-out 32-frame estimate must agree, and span >=1.5kHz. */
 if(good>=12&&(last-first)*93.75>=1500){
  memset(path_mask,0,sizeof path_mask);
  for(unsigned k=2;k<=53;k++)if(candidate_mask[k-2]){
   double norm=dot(path_candidate[k-2],path_candidate[k-2]).r;
   for(unsigned i=0;i<8;i++)path_gain[k][i]=sc(path_candidate[k-2][i],sqrt(8/norm));
   path_mask[k]=1;
  }
  path_angle=status.cal_angle;status.path_angle=path_angle;status.path_bins=good;status.cal_state=2;
 }else status.cal_state=3;
 status.cal_stage=3;
}
void ad_frame_done(void){
 status.frames++;status.epoch++;
 if(status.cal_state!=1||++cal_frames<32)return;
 if(status.cal_mode==1){path_finish_stage();return;}
 unsigned good=0;
 for(unsigned k=2;k<=53;k++){
  C *proposed=cal_proposed[k-2];AdCalBin *diag=&cal_diagnostics[k-2];
  unsigned flags=15;double hz=k*93.75,p0=live[k][0][0].r;
  diag->min_coherence=diag->min_amplitude=1e30;diag->max_amplitude=diag->max_phase_deg=0;
  double a0=2*PI*hz*60/343000*cos((90.0-status.cal_angle)*PI/180);
  for(unsigned i=0;i<8;i++){
   double power=live[k][i][i].r,coh=mag(live[k][i][0])/fmax(1e-30,p0*power);
   double p=2*PI*hz*60/343000*cos((90+45.0*i-status.cal_angle)*PI/180)-a0;
   proposed[i]=mul(sc(live[k][i][0],1/fmax(1e-30,p0)),(C){cos(p),-sin(p)});
   double amplitude=sqrt(mag(proposed[i])),phase=fabs(atan2(proposed[i].i,proposed[i].r))*180/PI;
   if(p0<1e-4||power<1e-4||!isfinite(power))flags&=~1U;
   if(!isfinite(coh)||coh<.90)flags&=~2U;
   if(!isfinite(amplitude)||amplitude<.5||amplitude>2)flags&=~4U;
   if(!isfinite(phase)||phase>45)flags&=~8U;
   diag->min_coherence=fmin(diag->min_coherence,coh);diag->min_amplitude=fmin(diag->min_amplitude,amplitude);
   diag->max_amplitude=fmax(diag->max_amplitude,amplitude);diag->max_phase_deg=fmax(diag->max_phase_deg,phase);
  }
  diag->flags=flags;
  if(flags&1)status.cal_power_bins++;
  if(flags&2)status.cal_coherent_bins++;
  if(flags&4)status.cal_amplitude_bins++;
  if(flags&8)status.cal_phase_bins++;
  if(flags==15)good++;
 }
 status.cal_bins=good;
 /* Preserve thresholds and the previous profile on rejection. */
 if(good>=12){
  for(unsigned k=2;k<=53;k++)if(cal_diagnostics[k-2].flags==15)memcpy(gain[k],cal_proposed[k-2],sizeof gain[k]);
  status.cal_state=2;
 }else status.cal_state=3;
 /* Do not train the operating covariance on the calibration-only source. */
 status.frames=0;memset(live,0,sizeof live);status.epoch++;
}
void ad_enable(unsigned enable){status.enabled=enable?1:0;status.epoch++;}
void ad_status(AdStatus *out){if(out)*out=status;}
int ad_calibration_bin(unsigned bin,AdCalBin *out){if(bin<2||bin>53||!out||(status.cal_state!=2&&status.cal_state!=3))return 0;*out=cal_diagnostics[bin-2];return 1;}
unsigned ad_epoch(void){return status.epoch;}
unsigned ad_freeze(void){
 memcpy(frozen,live,sizeof frozen);memcpy(frozen_gain,gain,sizeof gain);
 memcpy(frozen_path,path_gain,sizeof frozen_path);memcpy(frozen_path_mask,path_mask,sizeof path_mask);frozen_path_angle=path_angle;
 frozen_frames=(status.enabled&&status.cal_state!=1)?status.frames:0;
 return status.epoch;
}
static double response(unsigned k,const C w[8],double angle){C a[8];steer(k,angle,a);return sqrt(mag(dot(w,a)));}
int ad_weights(double hz,double target,double wr[8],double wi[8]){
 unsigned k=(unsigned)llround(hz/93.75);
 if(k<2||k>64||frozen_frames<16)return 0;
 C a[8],u[8],R[8][8],base[8],w[8];steer(k,target,a);
 double trace=0;for(unsigned i=0;i<8;i++)trace+=frozen[k][i][i].r;
 if(!isfinite(trace)||trace<8e-4)return 0;
 double norm=dot(a,a).r;
 for(unsigned i=0;i<8;i++){
  base[i]=sc(a[i],1/norm);
  for(unsigned j=0;j<8;j++)R[i][j]=sc(frozen[k][i][j],8/trace);
  R[i][i].r+=0.10; /* loaded MPDR/MVDR, no explicit target/noise mask */
 }
 if(!solve(R,a,u))return 0;
 C den=dot(a,u);for(unsigned i=0;i<8;i++)w[i]=divc(u[i],den);
 /* Optional measured-path LCMV null. Only when that path still explains
  * >=50% of current covariance energy and is >=45deg from the target.
  * Reject the null unless target-sector and white-noise guards both pass. */
 if(frozen_path_mask[k]&&fabs(remainder(target-frozen_path_angle,360.0))>=45){
  C q[8],v[8],rq[8],candidate[8];memcpy(q,frozen_path[k],sizeof q);
  for(unsigned i=0;i<8;i++){rq[i]=(C){0,0};for(unsigned j=0;j<8;j++)rq[i]=add(rq[i],mul(frozen[k][i][j],q[j]));}
  double evidence=dot(q,rq).r/fmax(1e-30,dot(q,q).r*trace);
  if(evidence>=.5&&solve(R,q,v)){
   C g00=dot(a,u),g01=dot(a,v),g10=dot(q,u),g11=dot(q,v);
   C determinant=sub(mul(g00,g11),mul(g01,g10));
   if(mag(determinant)>1e-16){
    C c0=divc(g11,determinant),c1=sc(divc(g10,determinant),-1);
    for(unsigned i=0;i<8;i++)candidate[i]=add(mul(u[i],c0),mul(v[i],c1));
    int valid=dot(candidate,candidate).r<=4/norm;
    for(int off=-10;off<=10;off+=5){double value=response(k,candidate,target+off);if(!isfinite(value)||value<pow(10,-1.0/20)||value>pow(10,1.0/20))valid=0;}
    if(valid)memcpy(w,candidate,sizeof w);
   }
  }
 }
 /* Convex shrink towards calibrated DAS bounds uncorrelated noise gain. */
 C d[8];for(unsigned i=0;i<8;i++)d[i]=sub(w[i],base[i]);
 double budget=3/norm,beta=fmin(1,sqrt(budget/fmax(1e-30,dot(d,d).r)));
 for(unsigned i=0;i<8;i++)w[i]=add(base[i],sc(d[i],beta));
 int protected=1;
 for(int offset=-10;offset<=10;offset+=5){double v=response(k,w,target+offset);if(v<pow(10,-1.0/20)||v>pow(10,1.0/20))protected=0;}
 if(!protected){status.rejected++;return 0;}
 for(unsigned i=0;i<8;i++){if(!isfinite(w[i].r)||!isfinite(w[i].i))return 0;wr[i]=w[i].r;wi[i]=w[i].i;}
 status.accepted++;return 1;
}
