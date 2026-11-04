#include "spatial_runtime.h"
#include "app_state.h"
#include "doa_capture.h"
#include "uptime.h"
#include "xil_io.h"
static SFSource sources[4];static unsigned source_count;
static uint32_t sequence,observation_at;static int fresh,supported;
static uint32_t read_register(unsigned r){return Xil_In32((UINTPTR)0x43C00000U+r);}
static void write_register(unsigned r,uint32_t v){Xil_Out32((UINTPTR)0x43C00000U+r,v);}
int spatial_runtime_init(void){
 source_count=sequence=observation_at=0;fresh=0;
 supported=sf_control_init(read_register,write_register);return supported;
}
void spatial_runtime_poll(void){
 if(!supported)return;
 uint32_t now=uptime_millis(),started=now;
 if((uint32_t)(now-observation_at)>=100){
  DoaDiagnostics d;doa_capture_diagnostics(&d);sequence=d.sequence;fresh=d.fresh;
  source_count=d.result.source_count>4?4:d.result.source_count;
  for(unsigned i=0;i<source_count;i++)sources[i]=(SFSource){d.result.sources[i].source_id,d.result.sources[i].angle_deg,d.result.sources[i].strength};
  observation_at=now;
 }
 sf_control_poll(now,app_state_get().target_angle_deg,sequence,fresh,sources,source_count);
 sf_control_timing(uptime_millis()-started);
}
void spatial_runtime_status(SFControlStatus *out){sf_control_status(out);}
unsigned spatial_runtime_active(void){return supported?(read_register(0x9C)&1U):0;}
int spatial_runtime_stream_ready(void){
 SFControlStatus s;sf_control_status(&s);
 /* Legacy PL keeps its original stream behaviour. FIR PL must finish its
  * first activation before exposing audio with a different signal delay. */
 return !s.supported||(spatial_runtime_active()&&s.updates!=0&&s.phase!=7);
}
