#include "spatial_design.h"
#include "adaptive.h"
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846
typedef struct {double r,i;} C;
static C add(C a,C b){return(C){a.r+b.r,a.i+b.i};}
static C sub(C a,C b){return(C){a.r-b.r,a.i-b.i};}
static C mul(C a,C b){return(C){a.r*b.r-a.i*b.i,a.r*b.i+a.i*b.r};}
static C scale(C a,double v){return(C){a.r*v,a.i*v};}
static C conjx(C a){return(C){a.r,-a.i};}
static double mag(C a){return a.r*a.r+a.i*a.i;}
static C divide(C a,C b){return scale(mul(a,conjx(b)),1.0/mag(b));}
static C dot(const C a[8],const C b[8]){C s={0,0};for(unsigned i=0;i<8;i++)s=add(s,mul(conjx(a[i]),b[i]));return s;}
static void steer(double hz,double angle,C a[8]){
 for(unsigned i=0;i<8;i++){double p=2*PI*hz*60.0/343000.0*cos((90+45.0*i-angle)*PI/180.0);a[i]=(C){cos(p),sin(p)};}
}
static int solve(C A[8][8],const C b[8],C out[8],unsigned n){
 C m[8][9];for(unsigned i=0;i<n;i++){for(unsigned j=0;j<n;j++)m[i][j]=A[i][j];m[i][n]=b[i];}
 for(unsigned k=0;k<n;k++){
  unsigned p=k;for(unsigned i=k+1;i<n;i++)if(mag(m[i][k])>mag(m[p][k]))p=i;
  if(mag(m[p][k])<1e-24)return 0;
  if(p!=k)for(unsigned j=k;j<=n;j++){C t=m[k][j];m[k][j]=m[p][j];m[p][j]=t;}
  C d=m[k][k];for(unsigned j=k;j<=n;j++)m[k][j]=divide(m[k][j],d);
  for(unsigned i=0;i<n;i++)if(i!=k){C f=m[i][k];for(unsigned j=k;j<=n;j++)m[i][j]=sub(m[i][j],mul(f,m[k][j]));}
 }
 for(unsigned i=0;i<n;i++)out[i]=m[i][n];
 return 1;
}
static void bounded(const C raw[8],const C das[8],C w[8]){
 C d[8];for(unsigned i=0;i<8;i++)d[i]=sub(raw[i],das[i]);
 double norm=dot(d,d).r,beta=norm>0?fmin(1.0,sqrt(0.375/norm)):1;
 for(unsigned i=0;i<8;i++)w[i]=add(das[i],scale(d[i],beta));
}
static double response(const C w[8],double hz,double angle){C a[8];steer(hz,angle,a);return 10*log10(fmax(1e-30,mag(dot(w,a))));}
static int preserves(const C w[8],double hz,double target){
 for(int i=-2;i<=2;i++)if(fabs(response(w,hz,target+5*i))>1.0)return 0;
 return 1;
}
static double leakage(const C w[8],double hz,double interference){
 double v=-1000;for(int i=-2;i<=2;i++)v=fmax(v,response(w,hz,interference+3.75*i));return v;
}
static int weights_loaded(double hz,double target,int interference,double penalty,double wr[8],double wi[8]){
 if(!isfinite(hz)||hz<0||!isfinite(target)||target<0||target>=360||interference< -1||interference>359)return 0;
 C a[8],das[8],w[8];steer(hz,target,a);for(unsigned i=0;i<8;i++)das[i]=w[i]=scale(a[i],0.125);
 double gap=interference<0?0:fabs(remainder(interference-target,360.0));
 if(hz>=80&&interference>=0&&gap>37.5){
  C R[8][8]={0},b[8],d[8],u[8],v[8],raw[8],point[8],flat[8];
  for(unsigned i=0;i<8;i++)R[i][i]=(C){1,0};
  for(int k=-2;k<=2;k++){
   steer(hz,interference+3.75*k,b);
   for(unsigned i=0;i<8;i++)for(unsigned j=0;j<8;j++)R[i][j]=add(R[i][j],scale(mul(b[i],conjx(b[j])),penalty/5));
  }
  for(unsigned i=0;i<8;i++){double dp=2*PI*hz*60/343000.0*sin((90+45.0*i-target)*PI/180.0);d[i]=mul(a[i],(C){0,dp});}
  if(solve(R,a,u,8)&&solve(R,d,v,8)){
   C au=dot(a,u);for(unsigned i=0;i<8;i++)raw[i]=divide(u[i],au);bounded(raw,das,point);
   C G[8][8]={0},f[8]={{1,0}},c[8];G[0][0]=au;G[0][1]=dot(a,v);G[1][0]=dot(d,u);G[1][1]=dot(d,v);
   if(solve(G,f,c,2)){
    for(unsigned i=0;i<8;i++)raw[i]=add(mul(u[i],c[0]),mul(v[i],c[1]));
    bounded(raw,das,flat);
    int pp=preserves(point,hz,target),fp=preserves(flat,hz,target);
    if(pp&&(!fp||leakage(point,hz,interference)<leakage(flat,hz,interference)))memcpy(w,point,sizeof w);
    else if(fp)memcpy(w,flat,sizeof w);
   }
  }
 }
 for(unsigned i=0;i<8;i++){if(!isfinite(w[i].r)||!isfinite(w[i].i))return 0;wr[i]=w[i].r;wi[i]=w[i].i;}return 1;
}
int sf_weights(double hz,double target,int interference,double wr[8],double wi[8]){
 if(hz>=0&&isfinite(hz)&&isfinite(target)&&target>=0&&target<360&&ad_weights(hz,target,wr,wi))return 1;
 if(!weights_loaded(hz,target,interference,1000,wr,wi))return 0;
 /* Strong sector penalties can waste the bounded noise budget at low
  * frequencies. Compare a less constrained solution, retaining exactly the
  * same target-sector protection and 4x DAS noise-power limit. Never replace
  * the old design unless its worst sampled interference leakage improves. */
 if(hz>=80&&hz<1500&&interference>=0){
  double lr[8],li[8];C old[8],candidate[8];
  if(weights_loaded(hz,target,interference,30,lr,li)){
   for(unsigned i=0;i<8;i++){old[i]=(C){wr[i],wi[i]};candidate[i]=(C){lr[i],li[i]};}
   if(preserves(candidate,hz,target)&&leakage(candidate,hz,interference)<leakage(old,hz,interference)-1e-9){
    memcpy(wr,lr,sizeof lr);memcpy(wi,li,sizeof li);
   }
  }
 }
 return 1;
}
static C spectrum[8][SF_TAPS];
static int32_t coefficients[SF_COEFFICIENTS];
static unsigned bin,channel,design_target,dc_cut;
static int design_interference,state; /* 0 idle,1 bins,2 inverse,3 ready,-1 error */
static unsigned design_error;
static void inverse_fft(C a[SF_TAPS]){
 for(unsigned i=1,j=0;i<SF_TAPS;i++){unsigned bit=SF_TAPS>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j){C t=a[i];a[i]=a[j];a[j]=t;}}
 for(unsigned len=2;len<=SF_TAPS;len*=2){C step={cos(2*PI/len),sin(2*PI/len)};
  for(unsigned start=0;start<SF_TAPS;start+=len){C w={1,0};for(unsigned j=0;j<len/2;j++){C v=mul(a[start+j+len/2],w),u=a[start+j];a[start+j]=add(u,v);a[start+j+len/2]=sub(u,v);w=mul(w,step);}}
 }
 for(unsigned i=0;i<SF_TAPS;i++)a[i]=scale(a[i],1.0/SF_TAPS);
}
int sf_design_begin(unsigned target,int interference,unsigned cut){
 if(target>359||interference< -1||interference>359||(cut!=0&&cut!=60))return 0;
 design_target=target;design_interference=interference;dc_cut=cut;bin=channel=0;design_error=0;state=1;return 1;
}
int sf_design_step(void){
 if(state==1){
  /* Exact DC endpoint: H(0)=0 for the DC-cut filter. Avoid evaluating a
   * cancellation-sensitive quotient or multiplying zero by an invalid weight. */
  if(bin==0&&dc_cut){for(unsigned c=0;c<8;c++)spectrum[c][0]=(C){0,0};bin++;return 0;}
  double wr[8],wi[8];if(!sf_weights(bin*48000.0/SF_TAPS,design_target,design_interference,wr,wi)){design_error=1;state=-1;return -1;}
  C hp={1,0};if(dc_cut){double p=2*PI*bin/SF_TAPS,pole=exp(-2*PI*dc_cut/48000.0);C z={cos(p),-sin(p)};hp=divide(sub((C){1,0},z),sub((C){1,0},scale(z,pole)));}
  for(unsigned c=0;c<8;c++){C h=mul((C){wr[c],-wi[c]},hp);if(!isfinite(h.r)||!isfinite(h.i)){design_error=2;state=-1;return -1;}if(bin==0||bin==SF_TAPS/2)h.i=0;spectrum[c][bin]=h;if(bin>0&&bin<SF_TAPS/2)spectrum[c][SF_TAPS-bin]=conjx(h);}
  if(++bin>SF_TAPS/2)state=2;
 }else if(state==2){
  inverse_fft(spectrum[channel]);int64_t sum=0;
  for(unsigned n=0;n<SF_TAPS;n++){
   double q=floor(spectrum[channel][(n+SF_TAPS/2)%SF_TAPS].r*262144.0+0.5);
   if(!isfinite(q)||q< -524288||q>524287){design_error=3;state=-1;return -1;}
   coefficients[channel*SF_TAPS+n]=(int32_t)q;sum+=(int32_t)q;
  }
  if(dc_cut){int64_t corrected=coefficients[channel*SF_TAPS+SF_TAPS/2]-sum;if(corrected< -524288||corrected>524287){design_error=4;state=-1;return -1;}coefficients[channel*SF_TAPS+SF_TAPS/2]=(int32_t)corrected;}
  if(++channel==8)state=3;
 }
 return state==3?1:state== -1?-1:0;
}
int32_t sf_design_coefficient(unsigned index){return state==3&&index<SF_COEFFICIENTS?coefficients[index]:0;}
unsigned sf_design_error(void){return design_error;}
