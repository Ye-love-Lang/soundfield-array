#include "xqspips.h"
#include "xil_cache.h"
#include <string.h>
#include <stdint.h>
static XQspiPs q;
int main(void){
 volatile uint32_t *s=(volatile uint32_t*)0x0FF00000U;
 uint8_t tx[4]={0x9f,0,0,0},rx[4]={0};
 s[0]=0x53463842U;s[1]=0;s[2]=0;s[3]=0x1000000U;
 XQspiPs_Config *c=XQspiPs_LookupConfig(0xE000D000U);
 if(!c||XQspiPs_CfgInitialize(&q,c,c->BaseAddress)!=0){s[1]=0xE001;goto end;}
 XQspiPs_SetOptions(&q,XQSPIPS_FORCE_SSELECT_OPTION|XQSPIPS_HOLD_B_DRIVE_OPTION);
 XQspiPs_SetClkPrescaler(&q,XQSPIPS_CLK_PRESCALE_8); XQspiPs_SetSlaveSelect(&q);
 if(XQspiPs_PolledTransfer(&q,tx,rx,4)!=0){s[1]=0xE002;goto end;}
 s[2]=((uint32_t)rx[1]<<16)|((uint32_t)rx[2]<<8)|rx[3];
 if(rx[3]!=0x18||rx[1]==0||rx[1]==255){s[1]=0xE003;goto end;}
 XQspiPs_LinearInit(0xE000D000U);
 XQspiPs_WriteReg(0xE000D000U,XQSPIPS_LQSPI_CR_OFFSET,0x8000010BU);
 memcpy((void*)0x10000000U,(void*)0xFC000000U,0x1000000U);
 {uint32_t crc=~0U;const uint8_t *p=(const uint8_t*)0x10000000U;
 for(unsigned i=0;i<0x1000000U;i++){crc^=p[i];for(unsigned b=0;b<8;b++)crc=(crc>>1)^(0xEDB88320U&-(int32_t)(crc&1));}
 s[4]=~crc;s[5]=*(const uint32_t*)0x10000000U;}
 Xil_DCacheFlushRange(0x10000000U,0x1000000U);s[1]=1;
end:Xil_DCacheFlushRange(0x0FF00000U,64);for(;;){}
}
