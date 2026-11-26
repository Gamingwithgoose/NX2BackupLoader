/* SPDX-License-Identifier: GPL-2.0-only */
/* Portable parser fixtures. Source only: not compiled/run by the agent.
 * cc -std=c11 -Wall -Wextra -Werror -I../source test_heapdef.c
 *    ../source/heapdef.c -o test_heapdef */
#include "heapdef.h"
#include <assert.h>
#include <string.h>
static void put32(unsigned char *p,uint32_t n,int be){
 for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(8*(be?3-i:i)));
}
static void header(unsigned char *p,unsigned type,unsigned count,int be){
 p[0]=(unsigned char)type;
 for(unsigned i=0;i<3;i++)p[i+1]=(unsigned char)(count>>(8*(be?2-i:i)));
}
static void entry(unsigned char *p,unsigned key,unsigned type,uint32_t value,int be){
 for(unsigned i=0;i<3;i++)p[i]=(unsigned char)(key>>(8*(be?2-i:i)));
 p[3]=(unsigned char)type;put32(p+4,value,be);
}
static void fixture(unsigned char b[256],int be){
 memset(b,0,256);b[0]=be?'B':'Y';b[1]=be?'Y':'B';b[2]=be?0:2;b[3]=be?2:0;
 put32(b+4,16,be);put32(b+12,128,be);
 header(b+16,0xc2,3,be);
 const char *keys[]={"Heaps","VirtualAddressMemory","Size"};
 unsigned at=20;
 for(unsigned i=0;i<3;i++){
  put32(b+20+4*i,at,be);size_t n=strlen(keys[i])+1;
  memcpy(b+16+at,keys[i],n);at+=(unsigned)n;
 }
 put32(b+32,at,be);
 header(b+128,0xc1,1,be);entry(b+132,0,0xc1,144,be);
 header(b+144,0xc1,1,be);entry(b+148,1,0xc1,160,be);
 header(b+160,0xc1,1,be);entry(b+164,2,0xd3,2048*1024,be);
}
int main(void){
 unsigned char b[256];uint64_t reserve=0;
 for(int be=0;be<2;be++){
  fixture(b,be);
  assert(nx2_heapdef_reserve(b,sizeof(b),&reserve)==1);
  assert(reserve==UINT64_C(2)*1024*1024*1024);
  /* Every truncated prefix must be safely rejected or lack the field. */
  for(size_t n=0;n<176;n++)assert(nx2_heapdef_reserve(b,n,&reserve)!=1);
  fixture(b,be);put32(b+168,UINT32_MAX,be);
  assert(nx2_heapdef_reserve(b,sizeof(b),&reserve)==-1);
  fixture(b,be);put32(b+148+4,UINT32_MAX,be);
  assert(nx2_heapdef_reserve(b,sizeof(b),&reserve)==-1);
  fixture(b,be);header(b+128,0xc1,0,be);
  assert(nx2_heapdef_reserve(b,sizeof(b),&reserve)==0);
  fixture(b,be);entry(b+164,2,0xd1,UINT32_MAX,be);
  assert(nx2_heapdef_reserve(b,sizeof(b),&reserve)==-1);
  fixture(b,be);entry(b+164,2,0xd5,248,be);
  /* Size is in KiB; a valid 64-bit scalar uses the remaining eight bytes. */
  put32(b+248,be?0:1024,be);put32(b+252,be?1024:0,be);
  assert(nx2_heapdef_reserve(b,sizeof(b),&reserve)==1&&reserve==1024*1024);
  assert(nx2_heapdef_reserve(b,255,&reserve)==-1);
 }
 assert(nx2_heapdef_reserve(NULL,0,&reserve)==-1);
 assert(nx2_heapdef_reserve(b,sizeof(b),NULL)==-1);
 return 0;
}
