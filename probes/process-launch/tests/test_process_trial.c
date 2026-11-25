/* SPDX-License-Identifier: GPL-2.0-only */
#include <switch.h>
#include "../source/process_trial.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned mode,hints=1,limits,created,started,terminated,closed_child,closed_limit,maps,unmaps;
static unsigned char page[4096] __attribute__((aligned(4096)));
bool envIsSyscallHinted(unsigned id){return hints && !(mode==1 && id==0x7a);}
Result svcGetInfo(u64 *out,u32 type,Handle h,u64 sub){
 assert(h==CUR_PROCESS_HANDLE && !sub);
 if(type==InfoType_CoreMask)*out=1;
 else if(type==InfoType_PriorityMask)*out=1ULL<<44;
 else if(type==InfoType_AslrRegionAddress)*out=(uintptr_t)page;
 else if(type==InfoType_AslrRegionSize)*out=sizeof(page);
 else assert(0);
 return 0;
}
Result svcGetProcessId(u64 *out,Handle h){assert(h==CUR_PROCESS_HANDLE||h==20);*out=h==20&&mode!=4?101:100;return 0;}
Result svcCreateResourceLimit(Handle *out){if(mode==2)return 0xe401;*out=10;return 0;}
Result svcSetResourceLimitLimitValue(Handle h,LimitableResource which,u64 value){
 static const u64 wanted[]={8ULL*1024*1024,1,0,0,0};
 assert(h==10 && which==limits && value==wanted[which]);limits++;
 return mode==3?0xe601:0;
}
Result svcCreateProcess(Handle *out,const void *p,const u32 *caps,u64 count){
 const unsigned char *b=p;u64 base;u32 resource;
 memcpy(&base,b+24,8);memcpy(&resource,b+40,4);
 assert(base==0x8000000 && resource==10 && count==3);
 assert(caps[0]==(7u|(44u<<4)|(44u<<10)));
 assert(caps[1]==(15u|(128u<<5)) && caps[2]==(32767u|(8u<<16)));
 if(mode==5)return 0xe801;
 created++;*out=20;return 0;
}
Result svcQueryMemory(MemoryInfo *m,u32 *pi,u64 address){assert(address==(uintptr_t)page);*pi=0;m->addr=address;m->size=4096;m->type=0;return 0;}
Result svcMapProcessMemory(void *p,Handle h,u64 src,u64 size){assert(p==page && h==20 && src==0x8000000 && size==4096);if(mode==6)return 0xc401;maps++;return 0;}
Result svcUnmapProcessMemory(void *p,Handle h,u64 src,u64 size){
 assert(p==page && h==20 && src==0x8000000 && size==4096);unmaps++;
 assert(((u32 *)page)[0]==0xd40000e1 && ((u32 *)page)[1]==0x14000000);
 return (mode==7&&unmaps==1)||mode==13?0xc601:0;
}
Result svcSetProcessMemoryPermission(Handle h,u64 src,u64 size,u32 perm){assert(h==20 && src==0x8000000 && size==4096 && perm==5);return mode==8?0xc801:0;}
Result svcStartProcess(Handle h,s32 priority,s32 core,u32 stack){assert(h==20 && priority==44 && core==0 && stack==4096);if(mode==9)return 0xca01;started++;return 0;}
Result svcGetProcessInfo(s64 *out,Handle h,unsigned which){assert(h==20 && !which);*out=!started?ProcessState_Created:mode==10||mode==12?ProcessState_Running:mode==11?ProcessState_Crashed:ProcessState_Exited;return 0;}
Result svcTerminateProcess(Handle h){assert(h==20);terminated++;return mode==12?0xda01:0;}
Result svcCloseHandle(Handle h){assert(h==10||h==20);if(h==10)closed_limit++;else closed_child++;return (h==20&&mode==14)||(h==10&&mode==15)?0xe401:0;}
void svcSleepThread(s64 delay){assert(delay==50000000);}
void armDCacheFlush(void *p,size_t n){assert(p==page && n==4096);}
static void reset(unsigned next){mode=next;limits=created=started=terminated=closed_child=closed_limit=maps=unmaps=0;}
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"w+");assert(f);
 hints=0;reset(0);assert(!nx2_process_trial(f,NULL));assert(!created&&!closed_limit&&!limits);
 hints=1;reset(1);assert(!nx2_process_trial(f,NULL));assert(!created&&!closed_limit&&!limits);
 reset(0);assert(nx2_process_trial(f,NULL));assert(created==1&&started==1&&closed_child==1&&closed_limit==1&&!terminated);
 for(unsigned i=2;i<=11;i++){
  reset(i);assert(!nx2_process_trial(f,NULL));
  assert(closed_limit==(i!=2));assert(closed_child==created);
  assert(terminated==created);
  if(i==7)assert(unmaps==2); /* Failed first unmap is retried during cleanup. */
 }
 for(unsigned i=12;i<=15;i++){
  reset(i);assert(nx2_process_trial(f,NULL)==-1);
  assert(closed_child==1 && closed_limit==1);
 }
 fclose(f);puts("Process boundary: permissions, distinct PID, payload, failures, timeout and cleanup PASS");return 0;
}
