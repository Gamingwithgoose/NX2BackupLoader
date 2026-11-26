/* SPDX-License-Identifier: GPL-2.0-only */
/* Mock native read-only API boundary, including forbidden-SVC avoidance. */
#define _CRT_SECURE_NO_WARNINGS
#include "resource_diagnostic.h"
#include <switch.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static unsigned mode,hints,open_calls,close_calls,quota_calls,info_calls;
static char output[16384];
uint64_t armGetSystemTick(void){return 123456;}
bool envIsSyscallHinted(unsigned n){return hints && (n==0x30||n==0x31||n==0x37);}
Result svcGetInfo(uint64_t *out,uint32_t type,Handle h,uint64_t sub){
 assert(sub==0);info_calls++;
 switch(type){
 case InfoType_ResourceLimit:
  assert(h==INVALID_HANDLE && hints);open_calls++;
  if(mode==1)return 0x16;
  *out=0x1234;return 0;
 case InfoType_SystemResourceSizeTotal:
  assert(h==CUR_PROCESS_HANDLE);*out=64ULL*1024*1024;return 0;
 case InfoType_SystemResourceSizeUsed:
  assert(h==CUR_PROCESS_HANDLE);
  if(mode==3)return 0xe16;
  *out=20ULL*1024*1024;return 0;
 case InfoType_FreeThreadCount:
  if(h!=CUR_PROCESS_HANDLE)return 0xe401;
  *out=3;return 0;
 case 31:assert(h==CUR_PROCESS_HANDLE);*out=192ULL*1024*1024;return mode==5?0xf001:0;
 case 32:assert(h==CUR_PROCESS_HANDLE);*out=12ULL*1024*1024;return mode==5?0xf201:0;
 default:assert(0);return 1;
 }
}
Result svcGetResourceLimitLimitValue(int64_t *out,Handle h,LimitableResource n){
 assert(hints && h==0x1234 && n<5);quota_calls++;
 *out=n==1?64:100;return 0;
}
Result svcGetResourceLimitCurrentValue(int64_t *out,Handle h,LimitableResource n){
 assert(hints && h==0x1234 && n<5);quota_calls++;
 *out=n==1?(mode==2?65:63):40;return 0;
}
Result svcGetResourceLimitPeakValue(int64_t *out,Handle h,LimitableResource n){
 assert(hints && h==0x1234 && n<5);quota_calls++;
 if(n==2)return 0x4321;
 *out=64;return 0;
}
Result svcCloseHandle(Handle h){assert(h==0x1234);close_calls++;return mode==4?0x159:0;}
static FILE *begin(const char *p){
 open_calls=close_calls=quota_calls=info_calls=0;
 FILE *f=fopen(p,"w+");assert(f);return f;
}
static void capture(FILE *f){
 assert(!fflush(f) && !fseek(f,0,SEEK_SET));
 size_t n=fread(output,1,sizeof(output)-1,f);output[n]=0;fclose(f);
}
int main(int argc,char **argv){
 assert(argc==2);Nx2ResourceDiagnostic d={0};
 FILE *f=begin(argv[1]);
 nx2_resource_diagnostic(f,&d,"pre_guest",0,48ULL*1024*1024,1);
 assert(!open_calls && !close_calls && !quota_calls && info_calls==6);
 capture(f);assert(strstr(output,"remaining=46137344"));
 assert(strstr(output,"free_threads_rc=00000000 free_threads=3"));
 assert(strstr(output,"info31_rc=00000000 info31=201326592 info32_rc=00000000 info32=12582912"));
 assert(strstr(output,"limit_attempted=0 current_attempted=0 peak_attempted=0"));
 assert(strstr(output,"limit=-1 current=-1 peak=-1 consistent=0 remaining=-1"));
 hints=1;d=(Nx2ResourceDiagnostic){0};f=begin(argv[1]);
 nx2_resource_diagnostic(f,&d,"runtime",100,48ULL*1024*1024,0);
 assert(open_calls==1 && close_calls==1 && quota_calls==15);
 nx2_resource_diagnostic(f,&d,"runtime",1099,0,0);
 assert(open_calls==1); /* No extra native calls before sample deadline. */
 capture(f);assert(strstr(output,"name=threads") && strstr(output,"limit=64 current=63 peak=64 consistent=1 remaining=1"));
 assert(strstr(output,"peak_rc=00004321"));
 mode=1;d=(Nx2ResourceDiagnostic){0};f=begin(argv[1]);
 nx2_resource_diagnostic(f,&d,"runtime",0,0,1);
 assert(open_calls==1 && !close_calls && !quota_calls);
 capture(f);assert(strstr(output,"handle_rc=00000016 handle_valid=0"));
 mode=2;d=(Nx2ResourceDiagnostic){0};f=begin(argv[1]);
 nx2_resource_diagnostic(f,&d,"runtime",0,0,1);capture(f);
 assert(strstr(output,"limit=64 current=65 peak=64 consistent=0 remaining=-1 at_limit=0 above_limit=1"));
 mode=3;d=(Nx2ResourceDiagnostic){0};f=begin(argv[1]);
 nx2_resource_diagnostic(f,&d,"runtime",0,0,1);capture(f);
 assert(strstr(output,"used_rc=00000e16") && strstr(output,"consistent=0 remaining=-1"));
 mode=4;d=(Nx2ResourceDiagnostic){0};f=begin(argv[1]);
 nx2_resource_diagnostic(f,&d,"runtime",0,0,1);
 nx2_resource_diagnostic(f,&d,"runtime",1000,0,1);
 assert(open_calls==1 && close_calls==1 && quota_calls==15);
 capture(f);assert(strstr(output,"disabled_after_close_failure=1"));
 mode=5;hints=0;d=(Nx2ResourceDiagnostic){0};d.declared_remote=256U*1024*1024;d.declared_remote_known=1;f=begin(argv[1]);
 nx2_resource_diagnostic(f,&d,"runtime",0,0,1);capture(f);
 assert(strstr(output,"info31_rc=0000f001") && strstr(output,"declared_exceeds_info31=0"));
 mode=0;d=(Nx2ResourceDiagnostic){0};d.declared_remote=256U*1024*1024;d.declared_remote_known=1;f=begin(argv[1]);
 nx2_resource_diagnostic(f,&d,"runtime",0,0,1);capture(f);
 assert(strstr(output,"declared_meta10_known=1 declared_meta10=268435456 declared_exceeds_info31=1"));
 puts("Resource diagnostics: permissions, accounting, remote fields, failures and throttle PASS");return 0;
}
