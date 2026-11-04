#include "spatial_runtime.h"
#include "protocol.h"
#include "uptime.h"
#include "app_state.h"
#include "audio_capture.h"
#include "adaptive.h"
#include "raw8.h"
#include "mic_capture.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);exit(1);}}while(0)
void test_reset(void);
static unsigned supported=1,enabled,bank,ready,pending,loaded,errors;
uint32_t Xil_In32(uintptr_t a){switch(a-0x43C00000U){case 0xA0:return supported?0x46495231:0;case 0x9C:return enabled|(bank<<1)|(ready<<2);case 0xA8:return errors;default:return 0;}}
void Xil_Out32(uintptr_t a,uint32_t v){switch(a-0x43C00000U){case 0x90:CHECK(v==0);loaded=ready=0;break;case 0x94:CHECK(!pending);if(++loaded==4096)ready=1;break;case 0x98:CHECK(!pending);pending=v;break;default:CHECK(0);}}
static char buffer[1040];
static const char *request(const char *cmd){char line[200];memset(buffer,0xA5,sizeof buffer);snprintf(line,sizeof line,"{\"v\":1,\"id\":1,\"cmd\":\"%s\",\"args\":{}}",cmd);size_t n=protocol_handle_transport(line,strlen(line),PROTOCOL_TRANSPORT_TCP_CONTROL,buffer+8,1024);CHECK(n>0&&n<1024);for(unsigned i=0;i<8;i++)CHECK((unsigned char)buffer[i]==0xA5&&(unsigned char)buffer[1032+i]==0xA5);return buffer+8;}
static void tick(unsigned t){if(pending){if(pending==3){enabled=1;bank^=1;ready=0;}else enabled=0;pending=0;}uptime_test_set(t);spatial_runtime_poll();}
static const char *uart_request(const char *cmd,const char *args){char line[220];memset(buffer,0xA5,sizeof buffer);snprintf(line,sizeof line,"{\"v\":1,\"id\":7,\"cmd\":\"%s\",\"args\":%s}",cmd,args);size_t n=protocol_handle(line,strlen(line),buffer+8,1024);CHECK(n>0&&n<1024);for(unsigned i=0;i<8;i++)CHECK((unsigned char)buffer[i]==0xA5&&(unsigned char)buffer[1032+i]==0xA5);return buffer+8;}
int main(void){
 ad_init();test_reset();CHECK(spatial_runtime_init());CHECK(strstr(request("START_STREAM"),"SPATIAL_NOT_READY"));
 for(unsigned t=0;t<1000;t++)tick(t);
 CHECK(spatial_runtime_active()&&spatial_runtime_stream_ready());
 CHECK(strstr(request("START_STREAM"),"\"port\":9000"));
 CHECK(strstr(request("GET_FILTER_STATUS"),"\"spatial_target_deg\":0"));
 CHECK(strstr(request("ARM_AUDIO_CAPTURE"),"UNSUPPORTED_TRANSPORT"));
 CHECK(strstr(uart_request("ARM_AUDIO_CAPTURE","{\"extra\":1}"),"INVALID_ARGUMENT"));
 CHECK(strstr(uart_request("ARM_AUDIO_CAPTURE","{}"),"\"state\":1"));
 CHECK(strstr(uart_request("GET_AUDIO_BLOCK","{\"offset\":0,\"count\":2}"),"CAPTURE_NOT_READY"));
 for(unsigned i=0;i<AC_DISCARD+AC_SAMPLES;i++)ac_push((i&1)?-32768:32767);
 mic_capture_audio_context();
 CHECK(strstr(uart_request("GET_AUDIO_CAPTURE","{}"),"\"valid\":1"));
 CHECK(strstr(uart_request("GET_AUDIO_BLOCK","{\"offset\":0,\"count\":2}"),"ff7f0080"));
 CHECK(strstr(uart_request("GET_AUDIO_BLOCK","{\"offset\":32767,\"count\":2}"),"CAPTURE_NOT_READY"));
 CHECK(strstr(uart_request("GET_AUDIO_BLOCK","{\"offset\":0,\"count\":129}"),"INVALID_ARGUMENT"));
 CHECK(strstr(uart_request("GET_AUDIO_BLOCK","{\"offset\":0,\"offset\":1,\"count\":2}"),"INVALID_ARGUMENT"));
 CHECK(strstr(uart_request("GET_AUDIO_BLOCK","{\"offset\":0,\"count\":128}"),"\"count\":128"));
 CHECK(strstr(request("GET_FILTER_STATUS"),"\"spatial_interference_deg\":360"));
 app_state_set_angle(90);for(unsigned t=1000;t<4000;t++)tick(t);
 CHECK(strstr(request("GET_FILTER_STATUS"),"\"spatial_target_deg\":90"));
 errors++;tick(4001);CHECK(!spatial_runtime_stream_ready());tick(4002);tick(4003);CHECK(!enabled);
 CHECK(strstr(request("START_STREAM"),"SPATIAL_NOT_READY"));
 supported=0;CHECK(!spatial_runtime_init());CHECK(spatial_runtime_stream_ready());
 CHECK(strstr(request("START_STREAM"),"\"port\":9000"));
 supported=enabled=1;ready=pending=0;CHECK(spatial_runtime_init());tick(5000);tick(5001);CHECK(!enabled&&!spatial_runtime_stream_ready());

 CHECK(strstr(uart_request("CAPTURE_RAW8","{}"),"\"armed\":true"));
 CHECK(strstr(request("CAPTURE_RAW8"),"UNSUPPORTED_TRANSPORT"));
 CHECK(strstr(uart_request("GET_RAW8_BLOCK","{\"offset\":0,\"count\":2,\"generation\":8}"),"000080ffffff7f00"));
 CHECK(strstr(uart_request("GET_RAW8_BLOCK","{\"offset\":0,\"count\":65,\"generation\":8}"),"INVALID_ARGUMENT"));
 CHECK(strstr(uart_request("GET_RAW8_BLOCK","{\"offset\":8192,\"count\":1,\"generation\":8}"),"RAW_NOT_READY"));
 CHECK(strstr(uart_request("GET_RAW8_BLOCK","{\"offset\":0,\"count\":1,\"generation\":9}"),"RAW_NOT_READY"));
 CHECK(strstr(uart_request("CALIBRATE_RAW8","{\"angle_deg\":360}"),"INVALID_ARGUMENT"));
 CHECK(strstr(uart_request("SET_ADAPTIVE","{\"enabled\":2}"),"INVALID_ARGUMENT"));
 CHECK(strstr(uart_request("SET_ADAPTIVE","{\"enabled\":0}"),"\"accepted\":true"));
 CHECK(strstr(uart_request("GET_ADAPTIVE_STATUS","{}"),"\"bits\":24"));

 CHECK(strstr(uart_request("GET_CALIBRATION_BIN","{\"bin\":1}"),"INVALID_ARGUMENT"));
 CHECK(strstr(request("GET_CALIBRATION_BIN"),"UNSUPPORTED_TRANSPORT"));
 CHECK(strstr(uart_request("GET_CALIBRATION_BIN","{\"bin\":2}"),"CALIBRATION_NOT_READY"));
 CHECK(ad_calibrate(180));for(unsigned f=0;f<32;f++)ad_frame_done();
 CHECK(strstr(uart_request("GET_ADAPTIVE_STATUS","{}"),"\"cal_power_bins\":0"));
 CHECK(strstr(uart_request("GET_CALIBRATION_BIN","{\"bin\":2}"),"\"max_phase_deg\":0.000000"));
 CHECK(strstr(uart_request("CALIBRATE_PATH8","{\"angle_deg\":360}"),"INVALID_ARGUMENT"));
 CHECK(strstr(uart_request("CALIBRATE_PATH8","{\"angle_deg\":180}"),"\"accepted\":true"));
 puts("{\"passed\":true,\"scope\":\"actual runtime/controller/design/protocol; AXI and network stubbed\",\"checks\":\"startup guard,retarget,diagnostics,buffer canaries,fault disable,legacy PL,PS-only restart\"}");return 0;
}


void raw8_init(void){}
void raw8_poll(void){}
int raw8_export_arm(void){return 1;}
int raw8_calibrate(unsigned a){return a<360;}
void raw8_status(Raw8Status *s){memset(s,0,sizeof *s);s->supported=1;s->export_ready=1;s->export_generation=8;s->export_last=1033;s->export_first=10;}
int raw8_read(unsigned o,unsigned n,int32_t *p){if(o>8192||n>8192-o)return 0;for(unsigned i=0;i<n;i++)p[i]=((i+o)&1)?8388607:-8388608;return 1;}

int raw8_calibrate_path(unsigned a){return a<360;}
