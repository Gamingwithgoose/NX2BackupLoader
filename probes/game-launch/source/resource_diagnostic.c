/* SPDX-License-Identifier: GPL-2.0-only */
#include "resource_diagnostic.h"
#include <switch.h>
#include <limits.h>
#define NOT_ATTEMPTED UINT32_MAX
void nx2_resource_diagnostic(FILE *f,Nx2ResourceDiagnostic *d,const char *phase,
                              uint64_t elapsed_ms,uint64_t declared,int force){
 if(!f || !d)return;
 if(!force && d->sequence && elapsed_ms>=d->last_elapsed_ms &&
    elapsed_ms-d->last_elapsed_ms<1000)return;
 uint64_t seq=++d->sequence;d->last_elapsed_ms=elapsed_ms;
 uint64_t tick=armGetSystemTick(),total=0,used=0,after=0,threads=0;
 Result total_rc=svcGetInfo(&total,InfoType_SystemResourceSizeTotal,CUR_PROCESS_HANDLE,0);
 Result used_rc=svcGetInfo(&used,InfoType_SystemResourceSizeUsed,CUR_PROCESS_HANDLE,0);
 Result after_rc=svcGetInfo(&after,InfoType_SystemResourceSizeTotal,CUR_PROCESS_HANDLE,0);
 /* Unlike ResourceLimit (type 9), FreeThreadCount resolves a process
  * handle. Passing INVALID_HANDLE yields InvalidHandle, not a quota count. */
 Result threads_rc=svcGetInfo(&threads,InfoType_FreeThreadCount,CUR_PROCESS_HANDLE,0);
 int valid=!total_rc && !used_rc && !after_rc && total==after && used<=total;
 int64_t remaining=valid && total-used<=INT64_MAX?(int64_t)(total-used):-1;
 if(seq==1)d->min_system_free=UINT64_MAX;
 if(valid){
  if(total-used<d->min_system_free)d->min_system_free=total-used;
  if(used>d->max_system_used)d->max_system_used=used;
 }
 fprintf(f,"SYSTEM_RESOURCE_OBSERVATION seq=%llu phase=%s elapsed_ms=%llu tick=%llu declared=%llu total_rc=%08x used_rc=%08x after_rc=%08x total=%llu used=%llu total_after=%llu consistent=%u remaining=%lld min_observed_remaining=%lld max_observed_used=%llu free_threads_rc=%08x free_threads=%lld\n",
  (unsigned long long)seq,phase,(unsigned long long)elapsed_ms,(unsigned long long)tick,
  (unsigned long long)declared,total_rc,used_rc,after_rc,
  (unsigned long long)total,(unsigned long long)used,(unsigned long long)after,valid,
  (long long)remaining,(long long)(d->min_system_free<=INT64_MAX?(int64_t)d->min_system_free:-1),
  (unsigned long long)d->max_system_used,threads_rc,
  (long long)(!threads_rc && threads<=INT64_MAX?(int64_t)threads:-1));
 /* Switch 2 remote-memory fields, also inspected by nx2-relink. Keep raw
  * IDs/results: their availability and meaning can vary with the host SDK.
  * These are a separate resource from SystemResourceSizeTotal/Used. Do not
  * reject a title or grant memory based on this observation. */
 uint64_t remote31=0,remote32=0;
 Result remote31_rc=svcGetInfo(&remote31,31,CUR_PROCESS_HANDLE,0);
 Result remote32_rc=svcGetInfo(&remote32,32,CUR_PROCESS_HANDLE,0);
 fprintf(f,"REMOTE_MEMORY_OBSERVATION seq=%llu phase=%s elapsed_ms=%llu info31_rc=%08x info31=%llu info32_rc=%08x info32=%llu declared_meta10_known=%u declared_meta10=%u declared_exceeds_info31=%u raw_fields=1 limit_changes=0\n",
  (unsigned long long)seq,phase,(unsigned long long)elapsed_ms,
  remote31_rc,(unsigned long long)remote31,remote32_rc,(unsigned long long)remote32,
  d->declared_remote_known,d->declared_remote,
  d->declared_remote_known && !remote31_rc && d->declared_remote>remote31);
 int can_limit=envIsSyscallHinted(0x30),can_current=envIsSyscallHinted(0x31);
 int can_peak=envIsSyscallHinted(0x37);
 uint64_t raw_handle=0;Handle handle=INVALID_HANDLE;
 uint32_t handle_rc=NOT_ATTEMPTED;
 int handle_attempted=(can_limit||can_current||can_peak) && !d->handle_close_failed;
 if(handle_attempted){
  /* InfoType_ResourceLimit is a current-process query with INVALID_HANDLE,
   * not a lookup using CUR_PROCESS_HANDLE. It returns an owned duplicate. */
  handle_rc=svcGetInfo(&raw_handle,InfoType_ResourceLimit,INVALID_HANDLE,0);
  if(!handle_rc && raw_handle && raw_handle<=UINT32_MAX)handle=(Handle)raw_handle;
 }
 fprintf(f,"RESOURCE_QUERY_CAPABILITY seq=%llu limit_hinted=%u current_hinted=%u peak_hinted=%u handle_attempted=%u handle_rc=%08x handle_valid=%u disabled_after_close_failure=%u values_nonatomic=1\n",
  (unsigned long long)seq,can_limit,can_current,can_peak,handle_attempted,handle_rc,
  handle!=INVALID_HANDLE,d->handle_close_failed);
 static const char *names[]={"memory","threads","events","transfer_memory","sessions"};
 for(unsigned i=0;i<5;i++){
  int64_t limit=-1,current=-1,peak=-1;
  uint32_t lr=NOT_ATTEMPTED,cr=NOT_ATTEMPTED,pr=NOT_ATTEMPTED;
  if(handle!=INVALID_HANDLE){
   if(can_limit)lr=svcGetResourceLimitLimitValue(&limit,handle,(LimitableResource)i);
   if(can_current)cr=svcGetResourceLimitCurrentValue(&current,handle,(LimitableResource)i);
   if(can_peak)pr=svcGetResourceLimitPeakValue(&peak,handle,(LimitableResource)i);
  }
  int values_valid=!lr && !cr && limit>=0 && current>=0;
  int coherent=values_valid && current<=limit;
  fprintf(f,"RESOURCE_QUOTA seq=%llu name=%s limit_attempted=%u current_attempted=%u peak_attempted=%u limit_rc=%08x current_rc=%08x peak_rc=%08x limit=%lld current=%lld peak=%lld consistent=%u remaining=%lld at_limit=%u above_limit=%u\n",
   (unsigned long long)seq,names[i],handle!=INVALID_HANDLE && can_limit,
   handle!=INVALID_HANDLE && can_current,handle!=INVALID_HANDLE && can_peak,
   lr,cr,pr,(long long)limit,(long long)current,(long long)peak,coherent,
   (long long)(coherent?limit-current:-1),coherent && limit==current,
   values_valid && current>limit);
 }
 if(handle!=INVALID_HANDLE){
  Result close_rc=svcCloseHandle(handle);
  if(close_rc)d->handle_close_failed=1;
  fprintf(f,"RESOURCE_QUERY_HANDLE_RELEASE seq=%llu result=%08x\n",(unsigned long long)seq,close_rc);
 }
}
