#include "spatial_runtime.h"
#include "protocol.h"
#include "uptime.h"
#include "app_state.h"
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
int main(void){
 test_reset();CHECK(spatial_runtime_init());CHECK(strstr(request("START_STREAM"),"SPATIAL_NOT_READY"));
 for(unsigned t=0;t<1000;t++)tick(t);
 CHECK(spatial_runtime_active()&&spatial_runtime_stream_ready());
 CHECK(strstr(request("START_STREAM"),"\"port\":9000"));
 CHECK(strstr(request("GET_FILTER_STATUS"),"\"spatial_target_deg\":0"));
 CHECK(strstr(request("GET_FILTER_STATUS"),"\"spatial_interference_deg\":360"));
 app_state_set_angle(90);for(unsigned t=1000;t<4000;t++)tick(t);
 CHECK(strstr(request("GET_FILTER_STATUS"),"\"spatial_target_deg\":90"));
 errors++;tick(4001);CHECK(!spatial_runtime_stream_ready());tick(4002);tick(4003);CHECK(!enabled);
 CHECK(strstr(request("START_STREAM"),"SPATIAL_NOT_READY"));
 supported=0;CHECK(!spatial_runtime_init());CHECK(spatial_runtime_stream_ready());
 CHECK(strstr(request("START_STREAM"),"\"port\":9000"));
 supported=enabled=1;ready=pending=0;CHECK(spatial_runtime_init());tick(5000);tick(5001);CHECK(!enabled&&!spatial_runtime_stream_ready());
 puts("{\"passed\":true,\"scope\":\"actual runtime/controller/design/protocol; AXI and network stubbed\",\"checks\":\"startup guard,retarget,diagnostics,buffer canaries,fault disable,legacy PL,PS-only restart\"}");return 0;
}
