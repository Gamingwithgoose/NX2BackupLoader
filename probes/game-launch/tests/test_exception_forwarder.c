/* SPDX-License-Identifier: GPL-2.0-only */
#define _CRT_SECURE_NO_WARNINGS
#include "exception_forwarder.h"
#include <switch.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned mode;
static unsigned char code[4096] __attribute__((aligned(4096)));
static char output[4096];
Result svcGetInfo(uint64_t *out,u32 type,u32 handle,uint64_t sub){assert(type==8&&!handle&&!sub);*out=0;return 0;}
Result svcQueryMemory(MemoryInfo *m,u32 *page,uint64_t at){
 if(mode==3)return 0xe401;
 uintptr_t base=(uintptr_t)code;*page=0;*m=(MemoryInfo){0};
 if(at<base){m->size=base;return 0;}
 if(at>=base+sizeof(code)){m->addr=UINT64_MAX;m->size=1;return 0;}
 m->addr=base;m->size=sizeof(code);m->type=3;m->perm=(mode==1&&at==base+0x300)?0:5;return 0;
}
int main(int argc,char **argv){
 assert(argc==2);
 for(mode=0;mode<4;mode++){
  memset(code,0,sizeof(code));u32 *w=(u32 *)code;w[2]=0x454d4f48;
  w[16]=mode==2?0xf0ffffe7U:0x90000007U; /* ADRP x7, previous/current page */
  w[17]=0xf94000e7U|(96U<<10);w[18]=0xb4000047U;w[19]=0xd61f00e0U;
  uint64_t value=0xabc;memcpy(code+0x300,&value,8);
  unsigned char before[sizeof(code)];memcpy(before,code,sizeof(code));
  FILE *f=fopen(argv[1],"w+");assert(f);nx2_exception_forwarder_audit(f,0xabc);
  assert(!memcmp(before,code,sizeof(code)));assert(!fseek(f,0,SEEK_SET));
  size_t n=fread(output,1,sizeof(output)-1,f);output[n]=0;fclose(f);
  assert(strstr(output,"read_only=1")&&strstr(output,"modified=0"));
  if(mode==0)assert(strstr(output,"matches_current_start=1"));
  if(mode==1||mode==2)assert(strstr(output,"readable=0")&&strstr(output,"matches_current_start=0"));
  if(mode==3)assert(strstr(output,"FORWARDER_QUERY_STOP result=0000e401"));
 }
 puts("Exception forwarder: read-only bounds, signed ADRP, unreadable target and query failure PASS");return 0;
}
