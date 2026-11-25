/* SPDX-License-Identifier: GPL-2.0-only */
#include "process_trial.h"
#include <switch.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

/* CreateProcess ABI: Atmosphere libvapours svc_types_priv.hpp lp64,
 * 0x30 bytes. libounce exposes the call as const void *. This experiment
 * does not claim the Switch 1 implementation proves Switch 2 compatibility. */
typedef struct {
 char name[12]; uint32_t version;
 uint64_t program_id, code_address;
 int32_t code_num_pages; uint32_t flags, reslimit;
 int32_t system_resource_num_pages;
} Nx2CreateProcessParameter;
_Static_assert(sizeof(Nx2CreateProcessParameter)==0x30,"process parameter size");
_Static_assert(offsetof(Nx2CreateProcessParameter,reslimit)==0x28,"resource offset");

static void report(FILE *f,FILE *crash,const char *fmt,...){
 va_list ap;va_start(ap,fmt);vfprintf(f,fmt,ap);va_end(ap);fflush(f);
 if(crash){va_start(ap,fmt);vfprintf(crash,fmt,ap);va_end(ap);fflush(crash);}
}
static void state(FILE *f,FILE *crash,Handle child,const char *phase,s64 *out){
 *out=-1;
 Result rc=svcGetProcessInfo(out,child,ProcessInfoType_ProcessState);
 report(f,crash,"CHILD_STATE phase=%s result=%08x state=%lld\n",phase,rc,(long long)*out);
}

int nx2_process_trial(FILE *f,FILE *crash){
 static const struct {unsigned id;const char *name;} required[]={
  {0x73,"SetProcessMemoryPermission"},{0x74,"MapProcessMemory"},
  {0x75,"UnmapProcessMemory"},{0x79,"CreateProcess"},
  {0x7a,"StartProcess"},{0x7b,"TerminateProcess"},
  {0x7c,"GetProcessInfo"},{0x7d,"CreateResourceLimit"},
  {0x7e,"SetResourceLimitLimitValue"}
 };
 unsigned missing=0;
 report(f,crash,"PROCESS_EXPERIMENT version=1 payload=exit_process_selftest game_launch_implemented=0 in_process_fallback=0\n");
 for(unsigned i=0;i<sizeof(required)/sizeof(required[0]);i++){
  int hinted=envIsSyscallHinted(required[i].id);
  report(f,crash,"PROCESS_CAPABILITY svc=%02x name=%s hinted=%d attempted=0\n",
         required[i].id,required[i].name,hinted);
  if(!hinted)missing++;
 }
 if(missing){
  report(f,crash,"PROCESS_RESULT status=BLOCKED stage=CAPABILITIES missing=%u child_created=0 game_started=0 reason=required_syscalls_not_advertised actual_kernel_permission_not_tested=1\n",missing);
  return 0;
 }
 /* Own limit object only. Never adjust the existing application's quotas.
  * No services, files, saves or game content are exposed to this child. */
 Handle limit=INVALID_HANDLE,child=INVALID_HANDLE;
 void *alias=NULL;int mapped=0,started=0,passed=0,exited=0,cleanup_failed=0;
 s64 current_state=-1;Result rc;
 u64 parent_pid=0,child_pid=0;
 rc=svcGetProcessId(&parent_pid,CUR_PROCESS_HANDLE);
 report(f,crash,"PARENT_PID result=%08x pid=%llu\n",rc,(unsigned long long)parent_pid);
 if(R_FAILED(rc)||!parent_pid)goto done;
 report(f,crash,"PROCESS_CALL phase=begin operation=CreateResourceLimit\n");
 rc=svcCreateResourceLimit(&limit);
 report(f,crash,"PROCESS_CALL phase=end operation=CreateResourceLimit result=%08x handle=%08x\n",rc,limit);
 if(R_FAILED(rc))goto done;
 static const u64 limits[]={8ULL*1024*1024,1,0,0,0};
 for(unsigned i=0;i<5;i++){
  report(f,crash,"PROCESS_CALL phase=begin operation=SetChildResourceLimit resource=%u value=%llu\n",i,(unsigned long long)limits[i]);
  rc=svcSetResourceLimitLimitValue(limit,(LimitableResource)i,limits[i]);
  report(f,crash,"PROCESS_CALL phase=end operation=SetChildResourceLimit resource=%u result=%08x\n",i,rc);
  if(R_FAILED(rc))goto done;
 }
 u64 core_mask=0,priority_mask=0;
 rc=svcGetInfo(&core_mask,InfoType_CoreMask,CUR_PROCESS_HANDLE,0);
 report(f,crash,"PARENT_CORE_MASK result=%08x value=%016llx\n",rc,(unsigned long long)core_mask);
 if(R_FAILED(rc)||!core_mask)goto done;
 rc=svcGetInfo(&priority_mask,InfoType_PriorityMask,CUR_PROCESS_HANDLE,0);
 report(f,crash,"PARENT_PRIORITY_MASK result=%08x value=%016llx\n",rc,(unsigned long long)priority_mask);
 if(R_FAILED(rc)||!priority_mask)goto done;
 unsigned core=0,priority=44;
 while(core<64 && !(core_mask&(1ULL<<core)))core++;
 if(!(priority_mask&(1ULL<<priority))){
  priority=0;while(priority<64 && !(priority_mask&(1ULL<<priority)))priority++;
 }
 /* Narrow child capability descriptors: one core/priority, ExitProcess,
  * eight handles. No guest NPDM permissions or privileged SVCs copied. */
 const u32 caps[]={7u|(priority<<4)|(priority<<10)|(core<<16)|(core<<24),
                   15u|((1u<<7)<<5),32767u|(8u<<16)};
 const Nx2CreateProcessParameter info={
  .name="NX2ProcTest",.version=0,.program_id=0,
  .code_address=0x8000000,.code_num_pages=1,
  .flags=1u|(3u<<1),.reslimit=limit,.system_resource_num_pages=0
 };
 report(f,crash,"PROCESS_CALL phase=begin operation=CreateProcess code=%016llx pages=1 flags=%08x core=%u priority=%u application=0\n",
        (unsigned long long)info.code_address,info.flags,core,priority);
 rc=svcCreateProcess(&child,&info,caps,sizeof(caps)/sizeof(caps[0]));
 report(f,crash,"PROCESS_CALL phase=end operation=CreateProcess result=%08x handle=%08x\n",rc,child);
 if(R_FAILED(rc))goto done;
 rc=svcGetProcessId(&child_pid,child);
 report(f,crash,"CHILD_PID result=%08x pid=%llu distinct=%u\n",rc,(unsigned long long)child_pid,child_pid!=parent_pid);
 if(R_FAILED(rc)||!child_pid||child_pid==parent_pid)goto done;
 /* Obtain an unmapped parent alias range; never overwrite malloc memory. */
 u64 region=0,region_size=0;
 rc=svcGetInfo(&region,InfoType_AslrRegionAddress,CUR_PROCESS_HANDLE,0);
 report(f,crash,"ALIAS_REGION_ADDRESS result=%08x value=%016llx\n",rc,(unsigned long long)region);
 if(R_FAILED(rc))goto done;
 rc=svcGetInfo(&region_size,InfoType_AslrRegionSize,CUR_PROCESS_HANDLE,0);
 report(f,crash,"ALIAS_REGION_SIZE result=%08x value=%llu\n",rc,(unsigned long long)region_size);
 if(R_FAILED(rc)||region_size<4096||region>UINTPTR_MAX-region_size)goto done;
 uintptr_t address=(uintptr_t)region;
 for(unsigned attempt=0;attempt<128;attempt++){
  MemoryInfo memory={0};u32 page=0;
  rc=svcQueryMemory(&memory,&page,address);
  if(R_FAILED(rc))goto done;
  if(memory.type==MemType_Unmapped && memory.size>=4096 && memory.addr<=address &&
     address-memory.addr<=memory.size-4096)break;
  if(memory.addr>UINTPTR_MAX-memory.size)goto done;
  address=(uintptr_t)(memory.addr+memory.size);
  if(attempt==127)goto done;
 }
 if(address<region||address>region+region_size-4096)goto done;
 alias=(void *)address;
 report(f,crash,"PROCESS_CALL phase=begin operation=MapProcessMemory alias=%016llx\n",(unsigned long long)address);
 rc=svcMapProcessMemory(alias,child,info.code_address,4096);
 report(f,crash,"PROCESS_CALL phase=end operation=MapProcessMemory result=%08x\n",rc);
 if(R_FAILED(rc))goto done;
 mapped=1;memset(alias,0,4096);
 /* svc #7 (ExitProcess), then b . if the SVC unexpectedly returns. */
 const u32 payload[]={0xd40000e1u,0x14000000u};
 memcpy(alias,payload,sizeof(payload));armDCacheFlush(alias,4096);
 rc=svcUnmapProcessMemory(alias,child,info.code_address,4096);
 report(f,crash,"PROCESS_CALL phase=end operation=UnmapProcessMemory result=%08x\n",rc);
 if(R_FAILED(rc))goto done;
 mapped=0;
 rc=svcSetProcessMemoryPermission(child,info.code_address,4096,Perm_Rx);
 report(f,crash,"PROCESS_CALL phase=end operation=SetProcessMemoryPermission result=%08x\n",rc);
 if(R_FAILED(rc))goto done;
 state(f,crash,child,"before_start",&current_state);
 if(current_state!=ProcessState_Created)goto done;
 report(f,crash,"PROCESS_CALL phase=begin operation=StartProcess stack=4096\n");
 rc=svcStartProcess(child,(s32)priority,(s32)core,4096);
 report(f,crash,"PROCESS_CALL phase=end operation=StartProcess result=%08x\n",rc);
 if(R_FAILED(rc))goto done;
 started=1;
 for(unsigned poll=0;poll<100;poll++){
  s64 observed=-1;rc=svcGetProcessInfo(&observed,child,ProcessInfoType_ProcessState);
  if(observed!=current_state||R_FAILED(rc))
   report(f,crash,"CHILD_STATE phase=running result=%08x state=%lld poll=%u\n",rc,(long long)observed,poll);
  current_state=observed;
  if(R_FAILED(rc)||observed==ProcessState_Crashed)break;
  if(observed==ProcessState_Exited){exited=1;passed=1;break;}
  svcSleepThread(50000000);
 }
done:
 if(mapped){
  rc=svcUnmapProcessMemory(alias,child,0x8000000,4096);
  report(f,crash,"PROCESS_CLEANUP operation=UnmapProcessMemory result=%08x\n",rc);
  if(R_FAILED(rc))cleanup_failed=1;
 }
 if(child!=INVALID_HANDLE){
  if(!exited){
   report(f,crash,"PROCESS_CLEANUP phase=begin operation=TerminateOwnChild handle=%08x\n",child);
   rc=svcTerminateProcess(child);
   report(f,crash,"PROCESS_CLEANUP phase=end operation=TerminateOwnChild result=%08x\n",rc);
   if(R_FAILED(rc))cleanup_failed=1;
  }
  rc=svcCloseHandle(child);report(f,crash,"PROCESS_CLEANUP operation=CloseChild result=%08x\n",rc);
  if(R_FAILED(rc))cleanup_failed=1;
 }
 if(limit!=INVALID_HANDLE){
  rc=svcCloseHandle(limit);report(f,crash,"PROCESS_CLEANUP operation=CloseChildLimit result=%08x\n",rc);
  if(R_FAILED(rc))cleanup_failed=1;
 }
 report(f,crash,"PROCESS_RESULT status=%s child_pid=%llu child_started=%u exited_observed=%u game_started=0 game_launch_implemented=0 cleanup_failed=%u\n",
        cleanup_failed?"CLEANUP_INCOMPLETE":passed?"CHILD_TEST_PASSED":"CHILD_TEST_FAILED",(unsigned long long)child_pid,started,exited,cleanup_failed);
 return cleanup_failed?-1:passed;
}
