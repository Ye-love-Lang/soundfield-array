#include "spatial_design.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 FILE *f=fopen("weight_reference.txt","r");CHECK(f);
 unsigned target;int inter;double hz,er[8],ei[8],wr[8],wi[8],maxerr=0;unsigned cases=0;
 while(fscanf(f,"%u %d %lf",&target,&inter,&hz)==3){
  for(unsigned i=0;i<8;i++)CHECK(fscanf(f,"%lf %lf",&er[i],&ei[i])==2);
  CHECK(sf_weights(hz,target,inter,wr,wi));double power=0,r=0,im=0;
  for(unsigned i=0;i<8;i++){
   maxerr=fmax(maxerr,hypot(wr[i]-er[i],wi[i]-ei[i]));power+=wr[i]*wr[i]+wi[i]*wi[i];
   double p=2*3.14159265358979323846*hz*60/343000*cos((90+45.0*i-target)*3.14159265358979323846/180);
   r+=wr[i]*cos(p)+wi[i]*sin(p);im+=wr[i]*sin(p)-wi[i]*cos(p);
  }
  CHECK(fabs(r-1)<1e-6&&fabs(im)<1e-6&&power*8<=4.00001);cases++;
 }
 fclose(f);CHECK(cases==1344&&maxerr<1e-7);
 CHECK(!sf_weights(NAN,90,180,wr,wi));CHECK(!sf_design_begin(360,180,0));CHECK(!sf_design_begin(90,360,0));
 CHECK(sf_design_begin(90,180,0));unsigned calls=0;int state;
 do{state=sf_design_step();CHECK(state>=0);calls++;CHECK(calls<300);}while(!state);
 f=fopen("fir_reference.txt","r");CHECK(f);int q;int maxq=0;
 for(unsigned i=0;i<4096;i++){CHECK(fscanf(f,"%d",&q)==1);int d=abs(q-sf_design_coefficient(i));if(d>maxq)maxq=d;CHECK(d<=1);}fclose(f);
 unsigned designs=0;
 for(unsigned t=0;t<360;t+=30)for(int kind=0;kind<3;kind++){
  CHECK(sf_design_begin(t,kind==0?-1:(int)((t+(kind==1?90:5))%360),60));
  calls=0;do{state=sf_design_step();CHECK(state>=0);CHECK(++calls<300);}while(!state);
  for(unsigned c=0;c<8;c++){int64_t sum=0;for(unsigned k=0;k<512;k++){int32_t v=sf_design_coefficient(c*512+k);CHECK(v>=-524288&&v<=524287);sum+=v;}CHECK(sum==0);}
  designs++;
 }
 printf("{\"passed\":true,\"weight_cases\":%u,\"max_complex_error\":%.12g,\"max_coefficient_lsb_error\":%d,\"dc_designs\":%u,\"steps_per_design\":265}\n",cases,maxerr,maxq,designs);
 return 0;
}
