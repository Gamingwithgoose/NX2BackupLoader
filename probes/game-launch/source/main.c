/* SPDX-License-Identifier: GPL-2.0-only */
#include "fault.h"
#include "../../../plugins/crash-monitor/source/monitor_wire.h"
#include "folder.h"
#include "heap_plan.h"
#include "guest_thread_policy.h"
#include "guest_core_policy.h"
#include "index_cache.h"
#include "nup.h"
#include "host.h"
#include "images.h"
#include "raw_svc.h"
#include "vm_release.h"
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <switch2/memory/heap.h>
static PadState pad;
/* The crash report is durable at snapshot boundaries, not after every line.
 * Separate, bursty fflush() calls previously competed with game SD reads. */
static FILE *nx2_deferred_crash_file;
static unsigned nx2_defer_crash_flush;
static char nx2_crash_buffer[32768];
static uint32_t selected_resource;
static char selected_save[256];
static char outcome[192] = "Mario Kart World override required.";
static int monitor_enabled;
static u64 monitor_title;
static int monitor_error_record(const char *fmt, ...){
  if(!monitor_enabled)return 0;
  FILE *m=fopen("sdmc:/kaihen/nx2-errors.pending","a");
  if(!m)return 0;
  fprintf(m,"tick=%llu title=%016llx ",(unsigned long long)armGetSystemTick(),(unsigned long long)monitor_title);
  va_list ap;va_start(ap,fmt);vfprintf(m,fmt,ap);va_end(ap);
  fputc('\n',m);return fclose(m)==0;
}
/* Explicit failure labels only; never mirror successful or warning lines. */
static int monitor_failure_label(const char *fmt){
  static const char *const keys[]={"REFUSED ","NUP_OPEN_FAILED","NUP_NPDM_FAILED",
    "ROMFS_CACHE_WRITE_FAILED","ROMFS_SCAN_FAILED","LAUNCH_PREPARATION_REFUSED",
    "WORKER_CREATE_REFUSED","NUP_CATALOG_REFUSED","NATIVE_CPU_FAULT",
    "FAULT_REGISTER","FAULT_PC_MODULE","FAULT_LR_MODULE"};
  for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++)
    if(!strncmp(fmt,keys[i],strlen(keys[i])))return 1;
  return 0;
}
static void row(FILE *f, const char *fmt, ...) {
  va_list a;
  va_start(a, fmt);
  vfprintf(f, fmt, a);
  va_end(a);
  if(monitor_enabled && monitor_failure_label(fmt)){
    char msg[1024];va_start(a,fmt);int n=vsnprintf(msg,sizeof(msg),fmt,a);va_end(a);
    if(n>0){size_t len=strlen(msg);while(len && msg[len-1]=='\n')msg[--len]=0;monitor_error_record("%s",msg);}
  }
  if(f!=nx2_deferred_crash_file || !nx2_defer_crash_flush)
    fflush(f);
}
/* All guest IPC requests and replies remain in the preallocated RAM ring.
 * Only important events are written to the SD by default. This avoids
 * defeating concurrent RomFS/game loading with dozens of thousands of
 * blocking SD-card text writes. Optional full text mode is for very short
 * diagnostic runs only, controlled by a user-created flag file. */
static void drain_ipc_events(FILE *f,Nx2Launch *l,uint32_t *cursor,
                             uint32_t *dropped,int full_text){
 uint32_t produced=__atomic_load_n(&l->ipc_event_next,__ATOMIC_ACQUIRE);
 if((uint32_t)(produced-*cursor)>NX2_IPC_EVENT_SLOTS){
  uint32_t lost=produced-*cursor-NX2_IPC_EVENT_SLOTS;
  fprintf(f,"IPC_EVENTS_DROPPED count=%u first=%u next=%u reason=ring_overflow\n",
          lost,*cursor,produced);
  *cursor+=lost;
  *dropped+=lost;
 }
 unsigned written=0;
 while(*cursor!=produced&&written<1024){
  uint32_t expected=*cursor+1;
  const Nx2IpcEvent *entry=&l->ipc_events[*cursor%NX2_IPC_EVENT_SLOTS];
  if(__atomic_load_n(&entry->sequence,__ATOMIC_ACQUIRE)!=expected)break;
  Nx2IpcEvent v=*entry;
  if(__atomic_load_n(&entry->sequence,__ATOMIC_ACQUIRE)!=expected)break;
  v.service[8]=0;
  /* Normal HID, applet and NVN polling dominates the IPC stream.
   * Keep those calls in RAM and in crash-tail snapshots. Disk records
   * focus on FSP interfaces, service acquisition and any error. */
  const int abnormal=(v.phase==2 &&
    ((v.kernel_result!=0 && v.kernel_result!=UINT32_MAX)||
     (v.cmif_result!=0 && v.cmif_result!=UINT32_MAX)));
  const int important=full_text||v.phase>=3||v.kind==2||v.kind==3||
                      abnormal||(v.phase==2&&v.kind==1&&v.command==1);
  const char *origin=v.phase==1?"REQUEST":v.phase==2?"KERNEL_RETURN":
                      v.phase==3?"GUEST_VIRTUAL_FS":
                      v.phase==4?"LOCAL_UNSUPPORTED_REPLY":
                      v.phase==5?"LOCAL_REJECT":"UNKNOWN";
  if(monitor_enabled && (abnormal || v.phase==4 || v.phase==5 || (v.phase==3 && ((v.kernel_result!=0 && v.kernel_result!=UINT32_MAX)||(v.cmif_result!=0 && v.cmif_result!=UINT32_MAX)))))
    monitor_error_record("IPC_FAILURE seq=%u call=%u phase=%u service=%s cmd=%u kernel=%08x cmif=%08x caller=%016llx",expected,v.call_id,v.phase,v.service,v.command,v.kernel_result,v.cmif_result,(unsigned long long)v.caller);
  if(important)
    fprintf(f,"IPC_EVENT seq=%u call=%u phase=%u origin=%s service=%s kind=%u handle=%08x hipc=%u command=%u domain_object=%08x flags=%02x hdr0=%08x hdr1=%08x special=%08x kernel=%08x cmif=%08x caller=%016llx tick=%llu\n",
            expected,v.call_id,v.phase,origin,v.service,v.kind,v.handle,
            v.request_type,v.command,v.object_id,v.flags,v.hdr0,v.hdr1,
            v.special,v.kernel_result,v.cmif_result,
            (unsigned long long)v.caller,(unsigned long long)v.tick);
  (*cursor)++;written++;
 }
}
static void screen(const char *message) {
  consoleClear();
  printf("BackupLoaderMKW 0.14\n\n%s\n", message);
  consoleUpdate(NULL);
}

static u64 monitor_last;
static u32 monitor_stage;
static u64 monitor_frames;
static int monitor_faulted;
static void monitor_emit(u32 phase,u64 title,u32 stage,u64 frames,int force){
  if(!monitor_enabled)return;
  u64 now=armGetSystemTick();
  if(!force && armTicksToNs(now-monitor_last)<5000000000ULL)return;
  monitor_last=now;
  if(phase==1||phase==2){monitor_title=title;monitor_stage=stage;monitor_frames=frames;}
  if(phase==3){title=monitor_title;stage=monitor_stage;frames=monitor_frames;if(monitor_faulted)phase=4;}
  u64 pid=0;
  svcGetProcessId(&pid,CUR_PROCESS_HANDLE);
  Nx2MonitorSample sample={NX2_MONITOR_MAGIC,phase,pid,title,now,frames,stage,0,now};
  FILE *m=fopen("sdmc:/kaihen/nx2-monitor.sample","wb");
  if(m){fwrite(&sample,1,sizeof(sample),m);fclose(m);}
}
static void __attribute__((noreturn)) stop(FILE *f, const char *why) {
  monitor_error_record("ABNORMAL_STOP reason=%s",why);
  monitor_emit(3,0,0,0,1);
  row(f, "STOP_PROCESS reason=%s\n", why);
  if(nx2_deferred_crash_file && nx2_deferred_crash_file!=f){
    row(nx2_deferred_crash_file,"STOP_PROCESS reason=%s\n",why);
    fflush(nx2_deferred_crash_file);
  }
  fclose(f);
  svcExitProcess();
}
static void game_entry(void *p) { nx2_game_worker(p); }
static int choose_core(FILE *f,int npdm_core) {
  u64 allowed = 0;
  Result rc = svcGetInfo(&allowed, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
  u32 current = svcGetCurrentProcessorNumber();
  if (R_FAILED(rc))return -1;
  int chosen=nx2_guest_select_core(allowed,(int)current,npdm_core);
  row(f,"WORKER_CORE host=%u selected=%d declared=%d allowed=%016llx source=%s\n",
      current,chosen,npdm_core,(unsigned long long)allowed,
      chosen==npdm_core?"npdm":"fallback");
  return chosen;
}
/* The guest's NPDM priority can differ from homebrew's legacy 44.
 * Honor it only within the actual Horizon process PriorityMask. Give the
 * dedicated storage worker a permitted, slightly higher urgency so a
 * CPU-bound guest cannot starve its own synchronous file operations. */
static int choose_priorities(FILE *f,int declared,int *guest,int *storage){
 if(!guest||!storage)return 0;
 u64 mask=0;
 Result rc=svcGetInfo(&mask,InfoType_PriorityMask,CUR_PROCESS_HANDLE,0);
 if(R_FAILED(rc)){
   *guest=0x2c;*storage=0x2b;
   row(f,"WORKER_PRIORITY_POLICY query=%08x declared=%d guest=%d storage=%d "
         "source=legacy_query_unavailable\n",rc,declared,*guest,*storage);
   return 1;
 }
 int p=nx2_guest_select_priority(mask,declared,0x2c);
 int q=nx2_storage_select_priority(mask,p);
 row(f,"WORKER_PRIORITY_POLICY query=%08x allowed=%016llx "
       "declared=%d guest=%d storage=%d source=%s\n",
     rc,(unsigned long long)mask,declared,p,q,
     p==declared?"npdm":p==0x2c?"legacy_permitted":"closest_permitted");
 if(p<0||q<0)return 0;
 *guest=p;*storage=q;
 return 1;
}
static int start_worker(FILE *f, Nx2Launch *l, Thread *t, int game,
                        uint32_t stack_bytes,int npdm_core,int priority,
                        int *selected_core) {
  int core = choose_core(f,npdm_core);
  if (core < 0)
    return 0;
  uint64_t host_free=heapGetFreeSize();
  row(f,"WORKER_STACK_RESOURCE requested=%u host_allocator_free=%llu\n",
      stack_bytes,(unsigned long long)host_free);
  if((uint64_t)stack_bytes>host_free){
    row(f,"WORKER_CREATE_REFUSED reason=insufficient_actual_host_memory "
          "requested=%u free=%llu\n",
        stack_bytes,(unsigned long long)host_free);
    return 0;
  }
  Result rc = threadCreate(t, game_entry, l, NULL, stack_bytes, priority, core);
  row(f, "WORKER_CREATE game=%d priority=%d result=%08x\n",game,priority,rc);
  if (R_FAILED(rc))
    return 0;
  l->worker_handle = t->handle;
  rc = threadStart(t);
  row(f, "WORKER_START game=%d handle=%08x result=%08x\n", game, t->handle, rc);
  if (R_FAILED(rc)) {
    threadClose(t);
    return 0;
  }
  if(selected_core)*selected_core=core;
  return 1;
}
/* Separate the filesystem host from the diagnostic main loop.
 * Guest RPC slots stay serialized through ONE service thread, preserving
 * filesystem handle/cache ownership while allowing independent watchdog
 * snapshots during slow SD/NUP reads. */
typedef struct {
  Nx2Host *host;
  Nx2Launch *launch;
  uint64_t last_poll_tick;
  uint64_t loop_count;
  uint32_t phase; /* 1 enter poll, 2 returned poll, 3 idle sleep */
  uint32_t handle;
  uint32_t running;
} Nx2StorageService;
static Nx2StorageService nx2_storage_service;
/* Bounded, host-only wait evidence. This never polls a guest service,
 * mutates an RPC slot, or tries to "unstick" the game with fake results. */
/* Reconstruct the last 64 GPU boundary events at a suspected stall.
 * Ring is best-effort under concurrency. Check each committed sequence
 * before and after copying, and skip records in the middle of overwrite.
 * Calls still run entirely native and no guest thread blocks for logging. */
static void report_gpu_boundary_history(FILE *out,Nx2Launch *l,uint64_t now){
 if(!out || !(l->nvn_gameplay_trace || l->nvn_driver_passthrough==0))return;
 uint32_t next=__atomic_load_n(&l->gpu_boundary_next,__ATOMIC_ACQUIRE);
 uint32_t count=next<64?next:64;
 row(out,"WARNING_GPU_BOUNDARY_HISTORY produced=%u last=%u "
         "kind=10/11_present_in/out_20/21_submit_22/23_flush_"
         "24/25_finish_26/27_window_acquire_28/29_queue_acquire\n",next,count);
 for(uint32_t i=count;i;i--){
  uint32_t ticket=next-i;
  const Nx2GpuBoundary *source=&l->gpu_boundary[ticket%NX2_GPU_BOUNDARY_RING_SLOTS];
  uint32_t sequence=__atomic_load_n(&source->sequence,__ATOMIC_ACQUIRE);
  if(sequence!=ticket+1)continue;
  Nx2GpuBoundary snapshot=*source;
  if(__atomic_load_n(&source->sequence,__ATOMIC_ACQUIRE)!=sequence)continue;
  uint64_t age=(snapshot.tick && now>=snapshot.tick)?
    armTicksToNs(now-snapshot.tick)/1000000ULL:0;
  row(out,"WARNING_GPU_BOUNDARY_EVENT seq=%u kind=%u age_ms=%llu "
          "tls=%016llx caller=%016llx queue=%016llx "
          "window=%016llx texture=%d detail=%016llx\n",
      sequence,snapshot.kind,(unsigned long long)age,
      (unsigned long long)snapshot.thread_tls,
      (unsigned long long)snapshot.caller,
      (unsigned long long)snapshot.queue,
      (unsigned long long)snapshot.window,snapshot.texture_index,(unsigned long long)snapshot.detail);
 }
}
static void report_unresolved_waits(FILE *out,Nx2Launch *l,Nx2Host *host,
                                    uint64_t now_tick,const char *reason){
 if(!out)return;
 uint64_t last=__atomic_load_n(&nx2_storage_service.last_poll_tick,__ATOMIC_ACQUIRE);
 uint64_t worker_age=now_tick>=last?
   armTicksToNs(now_tick-last)/1000000ULL:0;
 row(out,"WARNING_WORKER_LIVENESS reason=%s running=%u phase=%u "
         "poll_loops=%llu last_poll_age_ms=%llu active_slot=%u "
         "completed=%llu\n",
     reason,__atomic_load_n(&nx2_storage_service.running,__ATOMIC_ACQUIRE),
     __atomic_load_n(&nx2_storage_service.phase,__ATOMIC_ACQUIRE),
     (unsigned long long)__atomic_load_n(&nx2_storage_service.loop_count,__ATOMIC_ACQUIRE),
     (unsigned long long)worker_age,
     __atomic_load_n(&host->rpc_active_slot,__ATOMIC_ACQUIRE),
     (unsigned long long)__atomic_load_n(&host->rpc_completed_count,__ATOMIC_ACQUIRE));
 if(__atomic_load_n(&host->rpc_active_slot,__ATOMIC_ACQUIRE) &&
    __atomic_load_n(&host->rpc_active_op,__ATOMIC_RELAXED)==RPC_ROM_READ){
   uint32_t phase=__atomic_load_n(&host->rpc_rom_phase,__ATOMIC_ACQUIRE);
   uint32_t file=__atomic_load_n(&host->rpc_rom_file,__ATOMIC_RELAXED);
   const char *path=file<l->rom.count?nx2_rom_path(&l->rom,file):NULL;
   row(out,"WARNING_ROM_READ_STAGE reason=%s phase=%u file=%u "
           "offset=%llu bytes=%llu path=%s\n",reason,phase,file,
       (unsigned long long)__atomic_load_n(&host->rpc_rom_offset,__ATOMIC_RELAXED),
       (unsigned long long)__atomic_load_n(&host->rpc_rom_bytes,__ATOMIC_RELAXED),
       path?path:"[invalid/unavailable]");
   row(out,"WARNING_ROM_BACKEND_PROGRESS completed=%llu chunk_bytes=%u last_result=%08x "
           "note=phase8_open_phase9_fs_read\n",
       (unsigned long long)__atomic_load_n(&host->rpc_rom_completed,__ATOMIC_ACQUIRE),
       __atomic_load_n(&host->rpc_rom_chunk_bytes,__ATOMIC_RELAXED),
       __atomic_load_n(&host->rpc_rom_result,__ATOMIC_RELAXED));
 }
 if(worker_age>=1000){
   Handle h=__atomic_load_n(&nx2_storage_service.handle,__ATOMIC_ACQUIRE);
   if(h){
     s32 index=-1;
     Result rc=svcWaitSynchronization(&index,&h,1,0);
     /* Kernel success here means the thread termination handle is
      * signaled, NOT that the worker is idle or a ROM request succeeded. */
     row(out,"WARNING_WORKER_TERMINATION_PROBE handle=%08x "
             "kernel_result=%08x terminated=%u\n",
         h,rc,(unsigned)(R_SUCCEEDED(rc)));
   }
 }
 for(unsigned i=0;i<NX2_RPC_SLOTS;i++){
   Nx2Request *r=&l->requests[i];
   uint32_t state=__atomic_load_n(&r->state,__ATOMIC_ACQUIRE);
   if(!state)continue;
   if(state==1){
     /* Claimed but unpublished: request args are not initialized yet. */
     row(out,"WARNING_UNRESOLVED_RPC reason=%s slot=%u state=1 unpublished=1\n",
         reason,i);
     continue;
   }
   uint64_t published=__atomic_load_n(&r->diagnostic_published_tick,__ATOMIC_RELAXED);
   uint64_t age=(state>=2&&published&&now_tick>=published)?
     armTicksToNs(now_tick-published)/1000000ULL:0;
   row(out,"WARNING_UNRESOLVED_RPC reason=%s slot=%u state=%u "
           "op=%u age_ms=%llu ticket=%u owner_tls=%016llx "
           "arg_a=%016llx arg_b=%016llx arg_c=%016llx\n",
       reason,i,state,r->op,(unsigned long long)age,
       __atomic_load_n(&r->diagnostic_ticket,__ATOMIC_RELAXED),
       (unsigned long long)__atomic_load_n(&r->diagnostic_owner_tls,__ATOMIC_RELAXED),
       (unsigned long long)r->a,(unsigned long long)r->b,(unsigned long long)r->c);
 }
 unsigned reported=0;
 for(unsigned i=0;i<NX2_SYNC_FLIGHT_SLOTS;i++){
   Nx2SyncFlight *flight=&l->sync_flights[i];
   if(__atomic_load_n(&flight->phase,__ATOMIC_ACQUIRE)!=1)continue;
   uint64_t started=__atomic_load_n(&flight->started_tick,__ATOMIC_RELAXED);
   uint64_t age=started&&now_tick>=started?
     armTicksToNs(now_tick-started)/1000000ULL:0;
   if(age<1000)continue;
   row(out,"WARNING_NATIVE_SYNC_IN_FLIGHT reason=%s slot=%u age_ms=%llu "
           "tls=%016llx caller=%016llx handle=%08x hipc=%u "
           "raw_hdr0=%08x raw_hdr1=%08x call_id=%u\n",
       reason,i,(unsigned long long)age,
       (unsigned long long)flight->thread_tls,
       (unsigned long long)flight->caller,flight->handle,
       flight->request_type,flight->header0,flight->header1,flight->call_id);
   if(++reported==16)break;
 }
 row(out,"WARNING_SYNC_TRACE_COVERAGE collisions=%u "
         "pending_native_waits_reported=%u note=trace_not_kernel_stack\n",
   __atomic_load_n(&l->sync_flight_collisions,__ATOMIC_ACQUIRE),reported);
}

static int nx2_storage_core = -1;
static void storage_service_entry(void *opaque){
  Nx2StorageService *svc=(Nx2StorageService*)opaque;
  __atomic_store_n(&svc->running,1,__ATOMIC_RELEASE);
  for(;;){
    __atomic_store_n(&svc->phase,1,__ATOMIC_RELEASE);
    __atomic_store_n(&svc->last_poll_tick,armGetSystemTick(),__ATOMIC_RELEASE);
    int completed=nx2_host_poll(svc->host,svc->launch);
    __atomic_store_n(&svc->phase,2,__ATOMIC_RELEASE);
    __atomic_fetch_add(&svc->loop_count,1,__ATOMIC_RELAXED);
    __atomic_store_n(&svc->last_poll_tick,armGetSystemTick(),__ATOMIC_RELEASE);
    if(!completed){
      __atomic_store_n(&svc->phase,3,__ATOMIC_RELEASE);
      svcSleepThread(100000);
    }
  }
}
static int start_storage_service(FILE *f,Nx2Host *host,Nx2Launch *launch,
                                 Thread *thread,int priority){
  uint64_t allowed=0;
  uint32_t self=svcGetCurrentProcessorNumber();
  int core=-1;
  Result rc=svcGetInfo(&allowed,InfoType_CoreMask,CUR_PROCESS_HANDLE,0);
  if(R_SUCCEEDED(rc)){
    for(int k=5;k>=0;k--)
      if(k!=(int)self && (allowed&(UINT64_C(1)<<k))){core=k;break;}
  }
  if(core<0){
    row(f,"STORAGE_WORKER refused=no_available_core\n");
    return 0;
  }
  nx2_storage_service.host=host;
  nx2_storage_service.launch=launch;
  nx2_storage_service.last_poll_tick=armGetSystemTick();
  nx2_storage_service.running=0;
  nx2_storage_service.phase=0;
  nx2_storage_service.loop_count=0;
  nx2_storage_service.handle=0;
  /* Kernel-permitted scheduling priority selected from guest demand. */
  rc=threadCreate(thread,storage_service_entry,&nx2_storage_service,
                  NULL,0x80000,priority,core);
  row(f,"STORAGE_WORKER_CREATE core=%d priority=%d result=%08x\n",
      core,priority,rc);
  if(R_FAILED(rc))return 0;
  __atomic_store_n(&nx2_storage_service.handle,thread->handle,__ATOMIC_RELEASE);
  rc=threadStart(thread);
  row(f,"STORAGE_WORKER_START handle=%08x result=%08x\n",thread->handle,rc);
  if(R_FAILED(rc)){threadClose(thread);return 0;}
  nx2_storage_core=core;
  return 1;
}
/* Move only the launcher watchdog away from a guest's requested CPU.
 * Keep guest NPDM placement and the SD worker untouched. Failure to move
 * falls back to the original validated launch behavior. */
static void isolate_watchdog_core(FILE *f,int guest_core){
  u64 allowed=0;
  Result rc=svcGetInfo(&allowed,InfoType_CoreMask,CUR_PROCESS_HANDLE,0);
  int host_core=(int)svcGetCurrentProcessorNumber();
  if(R_FAILED(rc)){
    row(f,"WATCHDOG_CORE_MOVE skipped=mask_query result=%08x\n",rc);
    return;
  }
  int spare=nx2_watchdog_select_core(allowed,host_core,guest_core,
                                    nx2_storage_core);
  if(spare<0){
    row(f,"WATCHDOG_CORE_MOVE skipped=no_spare host=%d guest=%d storage=%d allowed=%016llx\n",
        host_core,guest_core,nx2_storage_core,(unsigned long long)allowed);
    return;
  }
  Result moved=svcSetThreadCoreMask(CUR_THREAD_HANDLE,spare,(u32)(1U<<spare));
  row(f,"WATCHDOG_CORE_MOVE host_before=%d target=%d guest=%d storage=%d result=%08x allowed=%016llx\n",
      host_core,spare,guest_core,nx2_storage_core,moved,
      (unsigned long long)allowed);
}
/* Staged scheduler recovery: expand affinity only after a genuine
 * observed multi-second present gap. Stage 2 is permitted only if frames
 * resume after stage 1, then stall again, and allows every permitted core.
 * Kernel-owned native operations and errors are never rewritten. */
static void attempt_stall_affinity_recovery(FILE *f,FILE *crash,
    Thread *guest, int guest_core,Thread *storage,int storage_core,
    unsigned recovery_stage){
 u64 allowed=0;
 Result query=svcGetInfo(&allowed,InfoType_CoreMask,CUR_PROCESS_HANDLE,0);
 if(R_FAILED(query) || guest_core<0 || storage_core<0){
   row(f,"WARNING_SCHEDULER_RECOVERY skipped=core_mask query=%08x guest_core=%d storage_core=%d\n",
       query,guest_core,storage_core);
   if(crash)row(crash,"WARNING_SCHEDULER_RECOVERY skipped=core_mask query=%08x\n",query);
   return;
 }
 /* Exclude watchdog's current CPU where possible but do not strand a
  * thread if the process has very few allowed CPUs. */
 u32 watchdog_core=svcGetCurrentProcessorNumber();
 u64 expanded=allowed & ~(UINT64_C(1)<<watchdog_core);
 u64 guest_mask=recovery_stage>=2 ? allowed :
    expanded & ~(UINT64_C(1)<<storage_core);
 u64 storage_mask=recovery_stage>=2 ? allowed :
    expanded & ~(UINT64_C(1)<<guest_core);
 if(!(guest_mask&(UINT64_C(1)<<guest_core)))guest_mask|=UINT64_C(1)<<guest_core;
 if(!(storage_mask&(UINT64_C(1)<<storage_core)))storage_mask|=UINT64_C(1)<<storage_core;
 guest_mask&=allowed;
 storage_mask&=allowed;
 Result g=svcSetThreadCoreMask(guest->handle,guest_core,guest_mask);
 Result st=svcSetThreadCoreMask(storage->handle,storage_core,storage_mask);
 row(f,"WARNING_SCHEDULER_RECOVERY recovery_stage=%u guest_handle=%08x guest_core=%d guest_mask=%016llx guest_rc=%08x "
       "storage_handle=%08x storage_core=%d storage_mask=%016llx storage_rc=%08x "
       "watchdog_core=%u cause=present_stall\n",
     recovery_stage,guest->handle,guest_core,(unsigned long long)guest_mask,g,
     storage->handle,storage_core,(unsigned long long)storage_mask,st,watchdog_core);
 if(crash)row(crash,"WARNING_SCHEDULER_RECOVERY recovery_stage=%u guest_handle=%08x guest_core=%d "
       "guest_mask=%016llx guest_rc=%08x storage_handle=%08x storage_core=%d "
       "storage_mask=%016llx storage_rc=%08x watchdog_core=%u\n",
     recovery_stage,guest->handle,guest_core,(unsigned long long)guest_mask,g,
     storage->handle,storage_core,(unsigned long long)storage_mask,st,watchdog_core);
}
static void write_fault_report(FILE *f, const Nx2Images *images) {
  const ThreadExceptionDump *d = &nx2_fault.dump;
  extern void _start(void);
  row(f,
      "NATIVE_CPU_FAULT description=%08x pc=%016llx lr=%016llx sp=%016llx "
      "fp=%016llx esr=%08x far=%016llx pstate=%08x NRO_BASE=%016llx TP=%016llx "
      "TLR=%016llx\n",
      d->error_desc, (unsigned long long)d->pc.x, (unsigned long long)d->lr.x,
      (unsigned long long)d->sp.x, (unsigned long long)d->fp.x, d->esr,
      (unsigned long long)d->far.x, d->pstate,
      (unsigned long long)(uintptr_t)_start,
      (unsigned long long)nx2_fault.tpidr, (unsigned long long)nx2_fault.tlr);
  row(f,"FAULT_ACCESS_CLASS ec=%02x iss=%08x far_valid_for_data_abort=%u write=%u\n",
      (unsigned)((d->esr>>26)&63),(unsigned)(d->esr&0x1ffffff),
      (unsigned)(((d->esr>>26)==0x24||(d->esr>>26)==0x25)&&!(d->esr&(1u<<10))),
      (unsigned)((d->esr>>6)&1));
  for (unsigned i = 0; i < 29; i++)
    row(f, "FAULT_REGISTER x%u=%016llx\n", i,
        (unsigned long long)d->cpu_gprs[i].x);
  if (images)
    for (unsigned i = 0; i < images->count; i++) {
      const Nx2Module *m = &images->modules[i];
      uint64_t begin = (uintptr_t)m->guest, end = begin + m->total - 4096;
      if (d->pc.x >= begin && d->pc.x < end)
        row(f, "FAULT_PC_MODULE module=%u offset=%llx\n", i,
            (unsigned long long)(d->pc.x - begin));
      if (d->lr.x >= begin && d->lr.x < end)
        row(f, "FAULT_LR_MODULE module=%u offset=%llx\n", i,
            (unsigned long long)(d->lr.x - begin));
    }

}
/* The existing libounce exception handler only copies CPU state. Report it
 * from the still-running host thread to both logs, without doing I/O on the
 * faulted thread's emergency stack. */
static void check_fault(FILE *f, FILE *crash, const Nx2Images *images) {
  if (__atomic_load_n(&nx2_fault.state, __ATOMIC_ACQUIRE) != 2)
    return;
  if (crash) {
    row(crash,"CRASH_CAPTURED_CPU_EXCEPTION exception_handler=libounce\n");
    write_fault_report(crash,images);
    /* A CPU fault is more important than batching; persist it now. */
    fflush(crash);
  }
  write_fault_report(f,images);
  if(monitor_enabled){
    FILE *e=fopen("sdmc:/kaihen/nx2-cpu-fault.txt","w");
    if(e){row(e,"CPU_FAULT_TICK %llu title=%016llx\n",(unsigned long long)armGetSystemTick(),(unsigned long long)monitor_title);write_fault_report(e,images);fclose(e);}
    monitor_faulted=1;
  }
  stop(f, "CPU fault captured; faulted thread remains parked; process exiting");
}
static int prepare_phase(FILE *f, const char *root, Nx2Images *images,
                         Nx2Launch *l, Nx2Host *host, const void *map,
                         size_t bytes, int game, uint64_t guest_id) {
  memset(l, 0, sizeof(*l));
  l->guest_program_id = guest_id;
  if (!nx2_rom_view(map, bytes, &l->rom))
    return 0;
  l->preflight_result = l->save_result = NX2_INVALID;
  for (unsigned i = 0; i < 3; i++)
    l->sample_results[i] = NX2_INVALID;
  l->arena_capacity = 0x4000000;
  l->arena_base = (uintptr_t)memalign(4096, l->arena_capacity);
  if (!l->arena_base)
    return 0;
  if (!nx2_host_init(host, f, root, selected_save,guest_id)) {
    free((void *)(uintptr_t)l->arena_base);
    l->arena_base = 0;
    return 0;
  }
  if (game) {
    const Nx2HeapPlan *p = &nx2_process_heap;
    row(f, "HBL_HEAP_OVERRIDE address=%016llx size=%llu\n",
        (unsigned long long)p->original_begin,
        (unsigned long long)p->original_bytes);
    row(f,
        "STARTUP_HEAP_PARTITION status=%u host_base=%016llx host_bytes=%llu "
        "host_allocator_total=%llu host_allocator_free=%llu\n",
        nx2_process_heap_status, (unsigned long long)p->host_begin,
        (unsigned long long)p->host_bytes,
        (unsigned long long)heapGetTotalSize(),
        (unsigned long long)heapGetFreeSize());
    if (nx2_process_heap_status != NX2_HEAP_READY) {
      row(f, "GAME_HEAP_UNAVAILABLE: startup reservation requires at least 768 "
             "MiB host plus 4 GiB game memory and alignment.\n");
      nx2_host_close(host);
      free((void *)(uintptr_t)l->arena_base);
      l->arena_base = 0;
      return 0;
    }
    row(f,
        "NATIVE_VM_RESERVATION release_candidate=%016llx bytes=%llu: reserved "
        "tail will return to kernel, not be zeroed.\n",
        (unsigned long long)p->game_begin, (unsigned long long)p->game_bytes);
  }

  if (!nx2_images_load(f, root, images, l, game)) {
    nx2_host_close(host);
    free((void *)(uintptr_t)l->arena_base);
    l->heap_base = l->arena_base = 0;
    return 0;
  }
  if (game) {
    screen("Returning unused heap memory for native game allocations...");
    if (!nx2_release_heap_for_vm(f, &nx2_process_heap, selected_resource, 1,
                                 NX2_GAME_MIN_BYTES, &l->heap_capacity))
      stop(f, "Native VM memory transition refused/failed; game not started");
    l->heap_base = 0;
  }
  return 1;
}
static int scan_cancelled;
static int scan_cancel(void *unused) {
  (void)unused;
  padUpdate(&pad);
  if (padGetButtonsDown(&pad) & HidNpadButton_B)
    scan_cancelled = 1;
  return scan_cancelled;
}
static void scan_progress(uint32_t files, void *unused) {
  (void)unused;
  char message[160];
  snprintf(message, sizeof(message), "Indexing game files... %u found\nPress B to cancel", files);
  screen(message);
}
/* Universal NVN modes: 0=debug, 1=native+present tracking, 2=all native. */
static void run(const Nx2Game *game, int rebuild_index, int nvn_profile) {
  monitor_faulted=0;
  if(monitor_enabled)remove("sdmc:/kaihen/nx2-cpu-fault.txt");
  monitor_emit(1,game->id,0,0,1);
  FILE *f = fopen(game->is_nup ? "sdmc:/BackupLoaderMKW-nup.log" :
                          "sdmc:/BackupLoaderMKW-gameplay.log", "w");
  if (!f) {
    snprintf(outcome, sizeof(outcome), "Could not create launch report.");
    return;
  }
  /* Separate, flushed diagnostic survives abrupt process termination where
   * the game cannot flush its own shutdown record. It is overwritten per run.
   * This cannot capture kernel/GPU faults that never reach the CPU handler. */
  FILE *crash = fopen("sdmc:/BackupLoaderMKW-crash.log","w");
  if(crash)
    setvbuf(crash,nx2_crash_buffer,_IOFBF,sizeof(nx2_crash_buffer));
  if (crash)
    row(crash,"NX2_CRASH_DIAGNOSTIC version=2 game=%s guest=%016llx host=0400c3f00006e000\n"
              "CAPTURE_MODE libounce_cpu_exception_plus_host_snapshot_and_prelaunch\n"
              "LIMITATION no_external_kernel_or_gpu_exception_monitor\n",
        game->name,(unsigned long long)game->id);

  const char *launch_phase="START";
  u64 launch_started=armGetSystemTick(), module_prepare_started=0;
  row(f,"REPORT_FORMAT version=3 source=%s crash_path=sdmc:/BackupLoaderMKW-crash.log\n",
      game->is_nup?"nup":"folder");
  if(crash)row(crash,"REPORT_FORMAT version=3 source=%s primary_path=%s\n",
      game->is_nup?"nup":"folder",
      game->is_nup?"sdmc:/BackupLoaderMKW-nup.log":"sdmc:/BackupLoaderMKW-gameplay.log");

  row(f,
      "BackupLoaderMKW experimental 0.15\nSELECTED_GAME name=%s root=%s "
      "program_id=%016llx\n",
      game->name, game->root, (unsigned long long)game->id);
  launch_phase="HOST_CHECK";
  u64 title = 0;
  Result rc = svcGetInfo(&title, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
  row(f, "HOST result=%08x title=%016llx\n", rc, (unsigned long long)title);
  if (R_FAILED(rc) || title != UINT64_C(0x0400c3f00006e000)) {
    row(f, "REFUSED MKW override required\n");
    fclose(f);
    snprintf(outcome, sizeof(outcome),
             "Start NX2 through Mario Kart World's override.");
    if(crash){row(crash,"PRELAUNCH_STOP phase=%s\n",launch_phase);fclose(crash);}
    return;
  }
  if (snprintf(selected_save, sizeof(selected_save), "%s", game->name) >= (int)sizeof(selected_save)) {
    row(f, "REFUSED save name too long\n");
    fclose(f);
    if(crash){row(crash,"PRELAUNCH_STOP phase=%s\n",launch_phase);fclose(crash);}
    return;
  }
  if(!game->is_nup)row(f,"CONTENT_ROOT_SELECTED path=%s update_version=%llu resolved_update=%u\n",
     game->root,(unsigned long long)game->update_version,
     (unsigned)(game->updates==2));
  launch_phase="PACKAGE_OPEN";
  selected_resource = game->resource;
  uint32_t declared_main_stack = game->main_stack_size;
  int declared_main_core = game->has_thread_metadata?(int)game->main_core:-1;
  int declared_main_priority =
      game->has_thread_metadata?(int)game->main_priority:-1;
  if(game->is_nup) {
    screen("Opening NUP game...");
    row(f,"NUP_LAUNCH_REQUEST path=%s\n",game->root);
    if(!nx2_nup_open(game->root)) {
      row(f,"NUP_OPEN_FAILED path=%s reason=%s\n",game->root,nx2_nup_probe_error());
      fclose(f);
      snprintf(outcome,sizeof(outcome),"NUP open failed: %.115s",nx2_nup_probe_error());
      if(crash){row(crash,"PRELAUNCH_STOP phase=%s\n",launch_phase);fclose(crash);}
      return;
    }
    row(f,"NUP_CONTENT_SELECTION result=%s\n",nx2_nup_probe_error());
    size_t npdm_size=0;
    uint64_t npdm_file_size=0;
    unsigned npdm_codec=0;
    int npdm_present=nx2_nup_file_info("exefs/main.npdm",
                                       &npdm_file_size,&npdm_codec);
    uint64_t packed=0,where=0;
    nx2_nup_entry_detail("exefs/main.npdm",&packed,&where);
    row(f,"NUP_NPDM_ENTRY present=%d bytes=%llu codec=%u stored=%llu extent=%llu\n",
        npdm_present,(unsigned long long)npdm_file_size,npdm_codec,
        (unsigned long long)packed,(unsigned long long)where);
    unsigned char *npdm=nx2_nup_read_all("exefs/main.npdm",0x100000,&npdm_size);
    row(f,"NUP_NPDM_READ status=%s reason=%s\n",
        npdm?"OK":"FAILED",nx2_nup_read_error());
    if(!npdm || npdm_size<128 || memcmp(npdm,"META",4)) {
      free(npdm);
      npdm=nx2_nup_package_npdm(&npdm_size);
      row(f,"NUP_NPDM_METADATA_FALLBACK status=%s bytes=%llu\n",
          npdm?"OK":"FAILED",(unsigned long long)npdm_size);
    }
    if(!npdm || npdm_size<128 || memcmp(npdm,"META",4)) {
      row(f,"NUP_NPDM_FAILED\n");
      free(npdm);
      nx2_nup_close();fclose(f);
      snprintf(outcome,sizeof(outcome),"NUP NPDM missing or unreadable; see NUP log.");
      if(crash){row(crash,"PRELAUNCH_STOP phase=%s\n",launch_phase);fclose(crash);}
      return;
    }
    memcpy(&selected_resource,npdm+0x14,4);
    memcpy(&declared_main_stack,npdm+0x1c,4);
    declared_main_priority=(int)npdm[0x0e];
    declared_main_core=(int)npdm[0x0f];
    free(npdm);
    row(f,"NUP_OPEN_OK title=%016llx resource=%u\n",
        (unsigned long long)game->id,selected_resource);
  }
  /* NPDM META+0x1c specifies the game's real main-thread stack.
   * Previously every game got a hardcoded 1 MiB guest worker stack.
   * Keep that minimum while honoring larger page-aligned stack requests.
   * The host allocator/kernel decides whether the request can be met. */
  uint32_t allocated_main_stack=0;
  if(!nx2_guest_main_stack(declared_main_stack,&allocated_main_stack)){
    row(f,"REFUSED NPDM main thread stack=%u reason=page_alignment_overflow\n",
        declared_main_stack);
    nx2_nup_close();
    fclose(f);
    snprintf(outcome,sizeof(outcome),
             "The game's declared main-thread stack is unsupported.");
    if(crash){row(crash,"PRELAUNCH_STOP phase=MAIN_THREAD_STACK\n");fclose(crash);}
    return;
  }
  row(f,"NPDM_MAIN_THREAD_STACK declared=%u allocated=%u minimum=%u "
        "policy=real_host_allocator_no_fixed_cap\n",
      declared_main_stack,allocated_main_stack,NX2_GUEST_STACK_BASE);
  row(f,"NPDM_MAIN_THREAD_CORE requested=%d policy=metadata_if_allowed\n",
      declared_main_core);
  row(f,"NPDM_MAIN_THREAD_PRIORITY requested=%d "
        "policy=metadata_if_kernel_priority_mask_allows\n",declared_main_priority);
  launch_phase="RESOURCE_CHECK";
  u64 resource = 0;
  rc = svcGetInfo(&resource, 16, CUR_PROCESS_HANDLE, 0);
  row(f, "RESOURCE_CHECK result=%08x available=%llu requested=%u\n", rc,
      (unsigned long long)resource, selected_resource);
  if (R_FAILED(rc) || resource < selected_resource) {
    row(f, "REFUSED insufficient host system resources\n");
    nx2_nup_close();
    fclose(f);
    snprintf(outcome, sizeof(outcome),
             "This backup requires more resources than MKW provides.");
    if(crash){row(crash,"PRELAUNCH_STOP phase=%s\n",launch_phase);fclose(crash);}
    return;
  }
  launch_phase="ROMFS_INDEX";
  screen(rebuild_index ? "Rebuilding game index..." : "Loading game index...");
  char rom[1400], error[160];
  if (snprintf(rom, sizeof(rom), "%s/romfs", game->root) >= (int)sizeof(rom)) {
    snprintf(outcome,sizeof(outcome),"Game path too long to construct RomFS.");
    nx2_nup_close();
    fclose(f);
    if(crash){row(crash,"PRELAUNCH_STOP phase=%s\n",launch_phase);fclose(crash);}
    return;
  }
  if(rebuild_index && !game->is_nup)nx2_folder_part_clear(rom);
  size_t map_bytes = 0;
  void *map = rebuild_index ? NULL : nx2_cache_load(rom, &map_bytes);
  if (map) {
    row(f, "ROMFS_CACHE_HIT root=%s bytes=%llu\n",
        rom, (unsigned long long)map_bytes);
  } else {
    row(f, game->is_nup ? "NUP_INDEX_BEGIN root=%s\n" :
                            "ROMFS_SCAN_BEGIN root=%s\n", rom);
    screen(game->is_nup ? "Building RomFS map from NUP table..." :
            "Indexing extracted game files...\nPress B to save progress and cancel.");
    u64 scan_began=armGetSystemTick();
    scan_cancelled = 0;
    nx2_folder_cancel(scan_cancel, NULL);
    nx2_folder_progress(scan_progress, NULL);
    map = nx2_folder_rom(rom, &map_bytes, error);
    nx2_folder_progress(NULL, NULL);
    nx2_folder_cancel(NULL, NULL);
    row(f,"INDEX_DURATION_MS=%llu RESUMED_FILES=%u\n",
        (unsigned long long)(armTicksToNs(armGetSystemTick()-scan_began)/1000000ULL),
        nx2_folder_resumed_files());
    if (scan_cancelled) {
      row(f, "ROMFS_SCAN_CANCELLED\n");
      free(map);
      nx2_nup_close();
      fclose(f);
      snprintf(outcome, sizeof(outcome), "Indexing cancelled. Press A to restart.");
      if(crash){row(crash,"PRELAUNCH_STOP phase=%s\n",launch_phase);fclose(crash);}
      return;
    }
    if (map && !game->is_nup) {
      if (nx2_cache_save(rom, map, map_bytes))
        row(f, "ROMFS_CACHE_SAVED bytes=%llu\n",
            (unsigned long long)map_bytes);
      else
        row(f, "ROMFS_CACHE_WRITE_FAILED\n");
    }
  }
  if (!map) {
    row(f, "ROMFS_SCAN_FAILED reason=%s\n", error);
    nx2_nup_close();
    fclose(f);
    snprintf(outcome, sizeof(outcome),
             game->is_nup ? "NUP RomFS index failed. See BackupLoaderMKW-nup.log." :
                           "Folder index failed. See BackupLoaderMKW-gameplay.log.");
    if(crash){row(crash,"PRELAUNCH_STOP phase=%s\n",launch_phase);fclose(crash);}
    return;
  }
  Nx2RomView view;
  if (!nx2_rom_view(map, map_bytes, &view)) {
    row(f, "ROMFS_INDEX_INVALID\n");
    snprintf(outcome,sizeof(outcome),"Game RomFS metadata failed validation.");
    free(map);
    nx2_nup_close();
    fclose(f);
    if(crash){row(crash,"PRELAUNCH_STOP phase=%s\n",launch_phase);fclose(crash);}
    return;
  }
  row(f, game->is_nup ? "NUP_ROMFS_INDEX_BUILT files=%u bytes=%llu virtual_bytes=%llu\n" :
                         "ROMFS_INDEX_BUILT files=%u bytes=%llu virtual_bytes=%llu\n",
      view.count, (unsigned long long)map_bytes,
      (unsigned long long)view.total);
  row(f,"CONTENT_INDEX_READY source=%s files=%u map_bytes=%llu virtual_bytes=%llu\n",
      game->is_nup?"nup":"folder",view.count,
      (unsigned long long)map_bytes,(unsigned long long)view.total);
  if(crash)row(crash,"CONTENT_INDEX_READY source=%s files=%u map_bytes=%llu virtual_bytes=%llu\n",
      game->is_nup?"nup":"folder",view.count,
      (unsigned long long)map_bytes,(unsigned long long)view.total);
  row(f,"PRELAUNCH_TIMING phase=CONTENT_READY cumulative_ms=%llu\n",
      (unsigned long long)(armTicksToNs(armGetSystemTick()-launch_started)/1000000ULL));
  launch_phase="PREPARE_MODULES";
  module_prepare_started=armGetSystemTick();
  Nx2Launch *l = calloc(1, sizeof(*l));
  Nx2Host *host = calloc(1, sizeof(*host));
  Nx2Images *images = calloc(1, sizeof(*images));
  if (!l || !host || !images) {
    snprintf(outcome, sizeof(outcome),
             "Not enough memory to prepare this backup.");
    goto done;
  }
  screen("Preparing the selected game...");
  if (!prepare_phase(f, game->root, images, l, host, map, map_bytes, 1, game->id)) {
    row(f, "LAUNCH_PREPARATION_REFUSED\n");
    snprintf(outcome, sizeof(outcome),
             game->is_nup ? "NUP preparation failed; see BackupLoaderMKW-nup.log." :
                            "Game preparation failed; see BackupLoaderMKW-gameplay.log.");
    goto done;
  }
  row(f,"MODULE_PREP_READY source=%s guest=%016llx module_count=%u physical_budget=%llu\n",
      game->is_nup?"nup":"folder",(unsigned long long)game->id,
      images->count,(unsigned long long)l->heap_capacity);
  row(f,"PRELAUNCH_TIMING phase=MODULE_READY elapsed_ms=%llu cumulative_ms=%llu\n",
      (unsigned long long)(armTicksToNs(armGetSystemTick()-module_prepare_started)/1000000ULL),
      (unsigned long long)(armTicksToNs(armGetSystemTick()-launch_started)/1000000ULL));
  if(crash)row(crash,"MODULE_PREP_READY source=%s guest=%016llx module_count=%u physical_budget=%llu\n",
      game->is_nup?"nup":"folder",(unsigned long long)game->id,
      images->count,(unsigned long long)l->heap_capacity);
  /* A restores the best-observed compatibility baseline; other paths
   * stay selectable for direct comparisons, independent of game title. */
  /* Single-file opt-in for the gameplay GPU boundary tracer. The A-mode
   * baseline stays untouched unless marker is explicitly present. */
  l->nvn_gameplay_trace=0;
  if(nvn_profile==1){
    FILE *gpu_trace=fopen("sdmc:/switch/BackupLoaderMKW/gameplay-gpu-trace.flag","rb");
    if(gpu_trace){l->nvn_gameplay_trace=1;fclose(gpu_trace);}
  }
  row(f,"GAMEPLAY_GPU_TRACE enabled=%u mode=%d functions=submit,flush,finish,window_acquire,queue_acquire,fence,wait,sync_wait,queue_error,pool_init,pool_finalize "
        "native_results_unchanged=1 source=%s\n",
      l->nvn_gameplay_trace,nvn_profile,
      l->nvn_gameplay_trace?"sd_flag":"default_off");
  if(crash)row(crash,"GAMEPLAY_GPU_TRACE enabled=%u nvn_mode=%d\n",
               l->nvn_gameplay_trace,nvn_profile);
  l->nvn_driver_passthrough=(uint32_t)nvn_profile;
  const char *profile=nvn_profile==2?"full_native":
                      nvn_profile==1?"native_present_observed":"diagnostic_full";
  row(f,"NVN_LAUNCH_MODE profile=%s mode=%d getproc_intercept=%u "
        "present_intercept=%u selected_by=%s\n",
      profile,nvn_profile,(unsigned)(nvn_profile!=2),(unsigned)(nvn_profile!=2),
      nvn_profile==2?"L+A":nvn_profile==1?"A":"ZR+A");
  if(crash)row(crash,"NVN_LAUNCH_MODE profile=%s mode=%d "
                     "getproc_intercept=%u present_intercept=%u\n",
               profile,nvn_profile,(unsigned)(nvn_profile!=2),
               (unsigned)(nvn_profile!=2));
  if(nvn_profile==2)
    row(f,"NVN_NATIVE_OBSERVABILITY bootstrap_only=1 "
          "getproc_and_present_counters_unobserved=1\n");
  launch_phase="GUEST_EXECUTE";
  row(f,"DIAGNOSTIC_PROFILE version=2 guest_fs_callsite=1 save_gate=1 exit_observer=existing CPU_exception_observer=libounce\n");
  if(crash)row(crash,"GUEST_EXECUTION_BEGIN phase=%s\n",launch_phase);
  row(f, "ACTUAL_ENTRY_ABOUT_TO_EXECUTE rtld=%016llx physical_budget=%llu\n",
      (unsigned long long)(uintptr_t)l->api.rtld,
      (unsigned long long)l->heap_capacity);
  screen("Starting game...");
  consoleExit(NULL);
  Result display_release = nwindowReleaseVi2Layer(nwindowGetDefault());
  row(f, "HOST_DISPLAY_RELEASE result=%08x\n", display_release);
  if (R_FAILED(display_release))
    stop(f, "Could not release launcher display");
  appletExit();
  row(f, "HOST_APPLET_PROXY_RELEASED\nGAMEPLAY_CUTOFF disabled\n");
  int guest_priority=0x2c,storage_priority=0x2b;
  if(!choose_priorities(f,declared_main_priority,
                        &guest_priority,&storage_priority))
    stop(f,"No kernel-permitted worker priority");
  Thread storage_worker;
  if(!start_storage_service(f,host,l,&storage_worker,storage_priority))
    stop(f,"Storage service worker could not start");
  Thread worker;
  int guest_core=-1;
  if (!start_worker(f, l, &worker, 1, allocated_main_stack,
                    declared_main_core,guest_priority,&guest_core))
    stop(f, "Game worker could not start");
  isolate_watchdog_core(f,guest_core);
  row(f,"PRELAUNCH_TIMING phase=GUEST_WORKER_STARTED cumulative_ms=%llu\n",
      (unsigned long long)(armTicksToNs(armGetSystemTick()-launch_started)/1000000ULL));
  unsigned affinity_recovery_stage=0;
  int affinity_recovery_armed=1;
  /* Experiment using the previously successful recovery configuration
   * preventively, before a stall, instead of waiting until the game has
   * already lost rendering progress. Opt in with ZR+A only; normal A and
   * L+A retain their previous scheduling policy. Native NVN/IPC behavior
   * is never substituted. */
  if(nvn_profile==0){
    affinity_recovery_stage=1;
    affinity_recovery_armed=0;
    row(f,"SCHEDULER_PREVENTIVE_POLICY mode=ZR+A stage=1 "
          "reason=preemptive_affinity_experiment\n");
    if(crash)row(crash,"SCHEDULER_PREVENTIVE_POLICY mode=ZR+A stage=1\n");
    attempt_stall_affinity_recovery(f,crash,&worker,guest_core,
                                    &storage_worker,nx2_storage_core,1);
    if(crash)fflush(crash);
  }
  u64 began = armGetSystemTick(), previous_frames = 0, heartbeat = began;
  uint64_t diagnostic_min_free=UINT64_MAX, diagnostic_max_io_age=0;
  uint64_t diagnostic_previous_tick=began, diagnostic_sequence=0;
  uint32_t diagnostic_gpu_cursor=0,diagnostic_shared_cursor=0;
  uint64_t diagnostic_memory_ok=0,diagnostic_memory_failed=0,diagnostic_memory_omitted=0;
  uint64_t diagnostic_gpu_omitted=0;
  u64 trace_sample = began;
  u64 ipc_event_flush=began;
  uint32_t ipc_event_cursor=0,ipc_event_dropped=0;
  /* Persist every observable guest IPC request and outcome in this
   * experimental diagnostic build. No payloads or UID buffers are copied.
   * Guest threads still only publish lock-free in-memory metadata. */
  /* Hardware full-text IPC overran the ring; keep errors, local
   * decisions and important service activity on disk by default. */
  int ipc_full_text=0;
  FILE *full_flag=fopen("sdmc:/switch/BackupLoaderMKW/full-ipc-trace.flag","rb");
  if(full_flag){ipc_full_text=1;fclose(full_flag);}
  /* Explicit sparse override has precedence over full trace. */
  FILE *sparse_flag=fopen("sdmc:/switch/BackupLoaderMKW/sparse-ipc-trace.flag","rb");
  if(sparse_flag){ipc_full_text=0;fclose(sparse_flag);}
  row(f,"IPC_CAPTURE mode=%s ring=%u request_reply=1 "
      "crash_tail=1 payloads=omitted local_decisions=labeled\n",
      ipc_full_text?"complete_request_result_metadata":"sparse_metadata_override",
      NX2_IPC_EVENT_SLOTS);
  row(f,"IPC_EVENT_PHASES 1=REQUEST 2=KERNEL_RETURN 3=GUEST_VIRTUAL_FS "
      "4=LOCAL_UNSUPPORTED_REPLY 5=LOCAL_REJECT\n");
  u64 crash_sample = began;
  uint32_t monitor_last_nvn_result[NX2_NVN_STAGE_COUNT]={0};
  u64 crash_detail_tick=began;
  int crash_detail_written=0;
  u32 previous_crash_ipc_next = UINT32_MAX;
  u32 previous_stage = UINT32_MAX, previous_trace_end = UINT32_MAX, previous_trace_begin = UINT32_MAX;
  u32 previous_graphics_ipc = UINT32_MAX;
  u32 last_graphics_bootstrap=UINT32_MAX,last_graphics_getproc=UINT32_MAX,
      last_graphics_present=UINT32_MAX;
  u32 last_nvn_device=UINT32_MAX,last_nvn_queue=UINT32_MAX,
      last_nvn_window=UINT32_MAX;
  u32 last_nvn_dev_done=UINT32_MAX,last_nvn_queue_done=UINT32_MAX,
      last_nvn_window_done=UINT32_MAX;
  uint32_t last_save_gate_phase=UINT32_MAX;
  uint32_t last_fsp_route_stage=UINT32_MAX;
  uint32_t last_nvn_stage[NX2_NVN_STAGE_COUNT]={0};
  uint32_t last_nvn_stage_done[NX2_NVN_STAGE_COUNT]={0};
  /* Host-only suspect-stall snapshot; no guest calls or changed dispatch. */
  u64 passive_last_frame=0,passive_frame_tick=began;
  u64 passive_ipc_at_progress=0,passive_rom_at_progress=0;
  int passive_reported=0;
  u64 passive_last_report_tick=0;
  u64 no_frame_last_report_tick=0;
  u64 last_unserved_rpc_warning_tick=began;
  /* Preserve flushed prelaunch evidence. Once guest execution begins,
   * batch independent crash-report writes and flush each complete sample.
   * Reduces SD sync I/O without weakening the CPU exception fast path. */
  if(crash){
    fflush(crash);
    nx2_deferred_crash_file=crash;
    nx2_defer_crash_flush=1;
  }
  for (;;) {
    check_fault(f, crash, images);
    /* The dedicated storage worker services guest RPCs; never block this
     * main/watchdog thread in an SD/NUP read. */
    /* Detect whether native save opening produced a usable virtual domain
     * object without making the guest block on any diagnostic RPC. */
    uint32_t fs_stage=__atomic_load_n(&l->fsp_route_stage,__ATOMIC_ACQUIRE);
    if(fs_stage!=last_fsp_route_stage){
      uint32_t mr=__atomic_load_n(&l->fsp_route_result,__ATOMIC_ACQUIRE);
      if(mr && mr!=UINT32_MAX)monitor_error_record("FSP_FAILURE stage=%u cmd=%u result=%08x",fs_stage,__atomic_load_n(&l->fsp_route_command,__ATOMIC_RELAXED),mr);
      row(f,"GUEST_FSP_ROUTE_STATE stage=%u cmd=%u object=%08x result=%08x child=%08x\n",
          fs_stage,
          __atomic_load_n(&l->fsp_route_command,__ATOMIC_RELAXED),
          __atomic_load_n(&l->fsp_route_object,__ATOMIC_RELAXED),
          __atomic_load_n(&l->fsp_route_result,__ATOMIC_RELAXED),
          __atomic_load_n(&l->fsp_route_child,__ATOMIC_RELAXED));
      last_fsp_route_stage=fs_stage;
    }
    drain_ipc_events(f,l,&ipc_event_cursor,&ipc_event_dropped,ipc_full_text);
    if(armTicksToNs(armGetSystemTick()-ipc_event_flush)>=2000000000ULL){
      ipc_event_flush=armGetSystemTick();
      fflush(f);
    }
    if (host->abort || __atomic_load_n(&l->abort_requested, __ATOMIC_ACQUIRE))
      stop(f, "Native launch requested stop");
    u32 stage = __atomic_load_n(&l->stage, __ATOMIC_ACQUIRE);
    if (stage != previous_stage) {
      row(f, "LAUNCH_STAGE %u\n", stage);
      previous_stage = stage;
    }
    u64 frames = __atomic_load_n(&l->frames, __ATOMIC_ACQUIRE);
    monitor_emit(2,game->id,stage,frames,0);
    /* The normal A mode counts actual returned PresentTexture calls.
     * A long interval without a present is not proof of a deadlock:
     * scenes can pause, load, suspend or deliberately stop presenting.
     * Native L+A is unobserved and must never produce this warning. */
    if(l->nvn_driver_passthrough!=2){
      u64 now=armGetSystemTick();
      if(frames!=passive_last_frame){
        passive_last_frame=frames;passive_frame_tick=now;
        passive_ipc_at_progress=
          __atomic_load_n(&l->ipc_trace_ends,__ATOMIC_ACQUIRE);
        passive_rom_at_progress=
          __atomic_load_n(&host->rpc_completed_count,__ATOMIC_ACQUIRE);
        passive_reported=0;
        passive_last_report_tick=0;
        /* Only a fresh sequence of completed presents rearms the next
         * recovery; never retry repeatedly within the same freeze. */
        if(affinity_recovery_stage==1 && !affinity_recovery_armed){
          affinity_recovery_armed=1;
          row(f,"SCHEDULER_RECOVERY_PROGRESS stage=1 frames=%llu next_stage_armed=1\n",
              (unsigned long long)frames);
          if(crash)row(crash,"SCHEDULER_RECOVERY_PROGRESS stage=1 frames=%llu next_stage_armed=1\n",
                       (unsigned long long)frames);
        }
      }else if(frames>=30 &&
         armTicksToNs(now-passive_frame_tick)>=8000000000ULL &&
         (!passive_reported ||
          armTicksToNs(now-passive_last_report_tick)>=10000000000ULL)){
        passive_reported=1;
        passive_last_report_tick=now;
        /* Stage 1 expands to other non-watchdog cores. If rendering
         * resumed and then froze again, stage 2 expands to all process-
         * permitted cores. Cap at two stages per launch. */
        if(affinity_recovery_armed && affinity_recovery_stage<2){
          affinity_recovery_stage++;
          affinity_recovery_armed=0;
          attempt_stall_affinity_recovery(f,crash,&worker,guest_core,
                                          &storage_worker,nx2_storage_core,
                                          affinity_recovery_stage);
          if(crash)fflush(crash);
        }
        if(!game->is_nup){
          row(f,"FOLDER_HOT_CACHE_STATS hits=%llu fills=%llu fill_failures=%llu "
                "read_ahead_hits=%llu read_ahead_fills=%llu\n",
              (unsigned long long)host->small_hits,
              (unsigned long long)host->small_fills,
              (unsigned long long)host->small_fill_failures,
              (unsigned long long)host->read_ahead_hits,
              (unsigned long long)host->read_ahead_fills);
          if(crash)row(crash,"FOLDER_HOT_CACHE_STATS hits=%llu fills=%llu "
                "fill_failures=%llu read_ahead_hits=%llu read_ahead_fills=%llu\n",
              (unsigned long long)host->small_hits,
              (unsigned long long)host->small_fills,
              (unsigned long long)host->small_fill_failures,
              (unsigned long long)host->read_ahead_hits,
              (unsigned long long)host->read_ahead_fills);
        }
        u64 ipc_now=__atomic_load_n(&l->ipc_trace_ends,__ATOMIC_ACQUIRE);
        u64 rom_now=__atomic_load_n(&host->rpc_completed_count,__ATOMIC_ACQUIRE);
        u32 slot=__atomic_load_n(&host->rpc_active_slot,__ATOMIC_ACQUIRE);
        u64 io_tick=__atomic_load_n(&host->rpc_started_tick,__ATOMIC_RELAXED);
        u64 io_age=slot && now>=io_tick?
          armTicksToNs(now-io_tick)/1000000ULL:0;
        u64 elapsed=armTicksToNs(now-passive_frame_tick)/1000000ULL;
        if(crash){
          row(crash,"WARNING_SUSPECTED_PRESENT_STALL duration_ms=%llu frames=%llu "
                    "ipc_end=%llu ipc_since_present=%llu rom_rpc=%llu "
                    "rom_rpc_since_present=%llu active_storage_slot=%u "
                    "storage_age_ms=%llu nvn_mode=%u confirmed_deadlock=0\n",
              (unsigned long long)elapsed,(unsigned long long)frames,
              (unsigned long long)ipc_now,
              (unsigned long long)(ipc_now-passive_ipc_at_progress),
              (unsigned long long)rom_now,
              (unsigned long long)(rom_now-passive_rom_at_progress),
              slot,(unsigned long long)io_age,l->nvn_driver_passthrough);
          row(crash,"WARNING_STALL_NATIVE_PRESENT entered=%u returned=%u "
                    "nvn_other_stages_observed=%u\n",
              __atomic_load_n(&l->nvn_present_entered,__ATOMIC_ACQUIRE),
              __atomic_load_n(&l->nvn_present_returned,__ATOMIC_ACQUIRE),
              (unsigned)(l->nvn_driver_passthrough==0 || l->nvn_gameplay_trace));
          u32 next=__atomic_load_n(&l->ipc_trace_next,__ATOMIC_ACQUIRE);
          for(u32 back=(next<8?next:8);back;back--){
            u32 slot_id=(next-back)%NX2_IPC_TRACE_SLOTS;
            Nx2IpcBreadcrumb *e=&l->ipc_breadcrumbs[slot_id];
            u32 phase=__atomic_load_n(&e->phase,__ATOMIC_ACQUIRE);
            if(!phase)continue;
            Nx2IpcBreadcrumb snap=*e;
            if(__atomic_load_n(&e->phase,__ATOMIC_ACQUIRE)!=phase)continue;
            snap.service[8]=0;
            row(crash,"WARNING_STALL_IPC_TAIL phase=%u service=%s "
                      "command=%u result=%08x cmif=%08x caller=%016llx\n",
                phase,snap.service,snap.command,snap.result,
                snap.service_result,(unsigned long long)snap.caller);
          }
          /* Guest-thread / graphics triage. All counts are sampled from
           * shared memory; no guest-side waits, heap allocation or IPC. */
          static const char *nvn_names[]={
            "submit","flush","finish","window_acquire","queue_acquire"
          };
          for(unsigned n=0;n<NX2_NVN_STAGE_COUNT;n++){
            row(crash,"WARNING_STALL_NVN_STAGE name=%s entered=%u returned=%u "
                      "result=%08x observed=%u\n",
                nvn_names[n],
                __atomic_load_n(&l->nvn_stage_entered[n],__ATOMIC_ACQUIRE),
                __atomic_load_n(&l->nvn_stage_returned[n],__ATOMIC_ACQUIRE),
                __atomic_load_n(&l->nvn_stage_result[n],__ATOMIC_ACQUIRE),
                (unsigned)(l->nvn_driver_passthrough==0 || l->nvn_gameplay_trace));
          }
          if(l->nvn_driver_passthrough==0 || l->nvn_gameplay_trace){
            for(unsigned n=0;n<NX2_NVN_STAGE_COUNT;n++){
              u64 started=__atomic_load_n(&l->nvn_stage_last_tick[n],__ATOMIC_ACQUIRE);
              u64 returned_tick=__atomic_load_n(&l->nvn_stage_return_tick[n],__ATOMIC_ACQUIRE);
              u32 entered=__atomic_load_n(&l->nvn_stage_entered[n],__ATOMIC_ACQUIRE);
              u32 returned=__atomic_load_n(&l->nvn_stage_returned[n],__ATOMIC_ACQUIRE);
              row(crash,"WARNING_STALL_NVN_PROVENANCE stage=%u entered=%u returned=%u "
                        "outstanding=%u caller=%016llx thread_tls=%016llx "
                        "last_enter_age_ms=%llu last_return_age_ms=%llu\n",
                  n,entered,returned,(unsigned)(entered!=returned),
                  (unsigned long long)__atomic_load_n(&l->nvn_stage_last_caller[n],__ATOMIC_ACQUIRE),
                  (unsigned long long)__atomic_load_n(&l->nvn_stage_last_tls[n],__ATOMIC_ACQUIRE),
                  (unsigned long long)(started && now>=started?
                                       armTicksToNs(now-started)/1000000ULL:0),
                  (unsigned long long)(returned_tick && now>=returned_tick?
                                       armTicksToNs(now-returned_tick)/1000000ULL:0));
            }
          }
          row(crash,"WARNING_STALL_GUEST_EXECUTION main_entered=%llu "
                    "main_returned=%u ipc_started=%u ipc_finished=%u "
                    "sync_collisions=%u storage_active=%u\n",
              (unsigned long long)__atomic_load_n(&l->main_entered,__ATOMIC_ACQUIRE),
              __atomic_load_n(&l->guest_main_returned,__ATOMIC_ACQUIRE),
              __atomic_load_n(&l->ipc_trace_begins,__ATOMIC_ACQUIRE),
              __atomic_load_n(&l->ipc_trace_ends,__ATOMIC_ACQUIRE),
              __atomic_load_n(&l->sync_flight_collisions,__ATOMIC_ACQUIRE),
              slot);
          /* A zero-timeout kernel check distinguishes a terminated guest
           * main worker from a living/stalled one; it does not reveal why a
           * living thread is blocked or its kernel stack. */
          {
            Handle gh=worker.handle;
            s32 signaled=-1;
            Result state_rc=svcWaitSynchronization(&signaled,&gh,1,0);
            row(crash,"WARNING_GUEST_WORKER_TERMINATION_PROBE handle=%08x "
                      "kernel_result=%08x terminated=%u\n",
                gh,state_rc,(unsigned)R_SUCCEEDED(state_rc));
          }
          report_gpu_boundary_history(crash,l,now);
          report_unresolved_waits(crash,l,host,now,"present_stalled");
          fflush(crash);
        }
        row(f,"WARNING_SUSPECTED_PRESENT_STALL duration_ms=%llu "
              "frames=%llu storage_slot=%u ipc_since_present=%llu "
              "rom_rpc_since_present=%llu details=crash_log\n",
            (unsigned long long)elapsed,(unsigned long long)frames,slot,
            (unsigned long long)(ipc_now-passive_ipc_at_progress),
            (unsigned long long)(rom_now-passive_rom_at_progress));
      }
    }
    /* Zero-frame games (notably OOT) need the same sync/guest execution
     * evidence without assuming that loading screens must Present. Emit
     * diagnostic snapshots at most once per 12 seconds from boot; these
     * cannot prove a deadlock by themselves. */
    if(crash && !frames && stage==10){
      u64 sample=armGetSystemTick();
      if(armTicksToNs(sample-began)>=12000000000ULL &&
         (!no_frame_last_report_tick ||
          armTicksToNs(sample-no_frame_last_report_tick)>=12000000000ULL)){
        no_frame_last_report_tick=sample;
        row(crash,"WARNING_ZERO_FRAME_GUEST_PROGRESS elapsed_ms=%llu "
                  "ipc_started=%u ipc_finished=%u nvn_bootstrap=%u "
                  "nvn_getproc=%u present_entered=%u present_returned=%u "
                  "confirmed_deadlock=0\n",
            (unsigned long long)(armTicksToNs(sample-began)/1000000ULL),
            __atomic_load_n(&l->ipc_trace_begins,__ATOMIC_ACQUIRE),
            __atomic_load_n(&l->ipc_trace_ends,__ATOMIC_ACQUIRE),
            __atomic_load_n(&l->nvn_bootstrap_calls,__ATOMIC_ACQUIRE),
            __atomic_load_n(&l->nvn_getproc_calls,__ATOMIC_ACQUIRE),
            __atomic_load_n(&l->nvn_present_entered,__ATOMIC_ACQUIRE),
            __atomic_load_n(&l->nvn_present_returned,__ATOMIC_ACQUIRE));
        report_unresolved_waits(crash,l,host,sample,"zero_frame_progress");
        fflush(crash);
      }
    }
    /* Capture persistent *published* RPCs even when there has never
     * been a first frame, and when normal presentation is still active.
     * Rate-limit to 10 seconds and only report after 3s unsatisfied. */
    if(crash &&
       armTicksToNs(armGetSystemTick()-last_unserved_rpc_warning_tick)>=10000000000ULL){
      uint64_t sample=armGetSystemTick();
      last_unserved_rpc_warning_tick=sample;
      for(unsigned i=0;i<NX2_RPC_SLOTS;i++){
        Nx2Request *request=&l->requests[i];
        uint32_t state=__atomic_load_n(&request->state,__ATOMIC_ACQUIRE);
        uint64_t published=__atomic_load_n(&request->diagnostic_published_tick,__ATOMIC_RELAXED);
        if(state==2 && published && sample>=published &&
           armTicksToNs(sample-published)>=3000000000ULL){
          report_unresolved_waits(crash,l,host,sample,"published_rpc_timeout");
          fflush(crash);
          break;
        }
      }
    }
    if (frames && (!previous_frames || frames - previous_frames >= 300)) {
      row(f, "GRAPHICS_PRESENT_RETURNED calls=%llu\n",
          (unsigned long long)frames);
      previous_frames = frames;
    }
    /* Lightweight graphics progress sampling independently of the slower
     * heartbeat: if this process terminates abruptly, the last flushed
     * entry still identifies which NVN phase it had reached. */
    uint32_t bcount=__atomic_load_n(&l->nvn_bootstrap_calls,__ATOMIC_ACQUIRE);
    uint32_t gcount=__atomic_load_n(&l->nvn_getproc_calls,__ATOMIC_ACQUIRE);
    uint32_t pcount=__atomic_load_n(&l->nvn_present_entered,__ATOMIC_ACQUIRE);
    if(bcount!=last_graphics_bootstrap ||
       gcount/256!=last_graphics_getproc/256 ||
       (pcount!=last_graphics_present &&
         (!last_graphics_present || pcount/120!=last_graphics_present/120))){
      row(f,"GRAPHICS_PROGRESS bootstrap=%u getproc=%u present_lookups=%u present_entered=%u present_returned=%u\n",
          bcount,gcount,
          __atomic_load_n(&l->nvn_present_lookups,__ATOMIC_ACQUIRE),
          pcount,__atomic_load_n(&l->nvn_present_returned,__ATOMIC_ACQUIRE));
      last_graphics_bootstrap=bcount;
      last_graphics_getproc=gcount;
      last_graphics_present=pcount;
    }
    uint32_t dev=__atomic_load_n(&l->nvn_device_entered,__ATOMIC_ACQUIRE);
    uint32_t que=__atomic_load_n(&l->nvn_queue_entered,__ATOMIC_ACQUIRE);
    uint32_t win=__atomic_load_n(&l->nvn_window_entered,__ATOMIC_ACQUIRE);
    uint32_t devdone=__atomic_load_n(&l->nvn_device_returned,__ATOMIC_ACQUIRE);
    uint32_t quedone=__atomic_load_n(&l->nvn_queue_returned,__ATOMIC_ACQUIRE);
    uint32_t windone=__atomic_load_n(&l->nvn_window_returned,__ATOMIC_ACQUIRE);
    if(dev!=last_nvn_device||que!=last_nvn_queue||win!=last_nvn_window||
       devdone!=last_nvn_dev_done||quedone!=last_nvn_queue_done||
       windone!=last_nvn_window_done){
      row(f,"NVN_INIT_PROGRESS device=%u/%u result=%08x queue=%u/%u result=%08x window=%u/%u result=%08x\n",
          dev,devdone,__atomic_load_n(&l->nvn_device_result,__ATOMIC_ACQUIRE),
          que,quedone,__atomic_load_n(&l->nvn_queue_result,__ATOMIC_ACQUIRE),
          win,windone,__atomic_load_n(&l->nvn_window_result,__ATOMIC_ACQUIRE));
      last_nvn_device=dev;last_nvn_queue=que;last_nvn_window=win;
      last_nvn_dev_done=devdone;last_nvn_queue_done=quedone;
      last_nvn_window_done=windone;
    }
    /* Early milestones are logged outside heartbeat so abrupt exits can
     * retain a last-known NVN stage; sample once on first entry/return,
     * then on power-of-two growth to avoid flooding the SD card. */
    for(unsigned n=0;n<NX2_NVN_STAGE_COUNT;n++){
      uint32_t entered=__atomic_load_n(&l->nvn_stage_entered[n],__ATOMIC_ACQUIRE);
      uint32_t returned=__atomic_load_n(&l->nvn_stage_returned[n],__ATOMIC_ACQUIRE);
      uint32_t mr=__atomic_load_n(&l->nvn_stage_result[n],__ATOMIC_ACQUIRE);
      if(returned && mr && mr!=UINT32_MAX && mr!=monitor_last_nvn_result[n])monitor_error_record("NVN_FAILURE stage=%u result=%08x caller=%016llx",n,mr,(unsigned long long)__atomic_load_n(&l->nvn_stage_last_caller[n],__ATOMIC_ACQUIRE));
      monitor_last_nvn_result[n]=mr;
      if((entered!=last_nvn_stage[n]||returned!=last_nvn_stage_done[n]) &&
         (last_nvn_stage[n]==0||last_nvn_stage_done[n]==0||
          (entered && !(entered&(entered-1)))||
          (returned && !(returned&(returned-1))))){
        static const char *names[]={"submit","flush","finish","window_acquire","queue_acquire"};
        row(f,"NVN_STAGE_PROGRESS name=%s entered=%u returned=%u latest_result=%08x\n",
            names[n],entered,returned,
            __atomic_load_n(&l->nvn_stage_result[n],__ATOMIC_ACQUIRE));
      }
      last_nvn_stage[n]=entered;
      last_nvn_stage_done[n]=returned;
    }
    /* Maintain an independently flushed process-end breadcrumb. A final
     * snapshot with no exception record means only that the app disappeared
     * without invoking the libounce handler; it is not a kernel diagnosis. */
    /* Capture short-lived exits before 15 seconds or first presentation,
     * without paying the 100-ms SD logging cost throughout gameplay. */
    uint64_t since_start=armTicksToNs(armGetSystemTick()-began);
    /* Early no-frame boot still gets subsecond diagnostics, but regular
     * snapshots must not compete with game asset reads every 100-500ms. */
    uint64_t crash_interval=(since_start<15000000000ULL && !frames)
                          ?250000000ULL:2000000000ULL;
    int captured_snapshot=0;
    if(crash && armTicksToNs(armGetSystemTick()-crash_sample)>=crash_interval){
      captured_snapshot=1;
      crash_sample=armGetSystemTick();
      uint64_t allocator_total=heapGetTotalSize();
      uint64_t allocator_free=heapGetFreeSize();
      if(allocator_free<diagnostic_min_free)diagnostic_min_free=allocator_free;
      row(crash,"HOST_ALLOCATOR_SNAPSHOT seq=%llu elapsed_ms=%llu sample_gap_ms=%llu total=%llu free=%llu min_sampled_free=%llu\n",
          (unsigned long long)++diagnostic_sequence,
          (unsigned long long)(armTicksToNs(crash_sample-began)/1000000ULL),
          (unsigned long long)(armTicksToNs(crash_sample-diagnostic_previous_tick)/1000000ULL),
          (unsigned long long)allocator_total,(unsigned long long)allocator_free,
          (unsigned long long)diagnostic_min_free);
      diagnostic_previous_tick=crash_sample;
      /* Keep startup evidence; aggregate routine gameplay records.
       * Sampling and omission are explicitly labelled, never inferred as success. */
      int compact_gameplay=frames!=0;
      uint32_t shared_next=__atomic_load_n(&l->shared_trace_next,__ATOMIC_ACQUIRE);
      uint32_t shared_pending=shared_next-diagnostic_shared_cursor;
      if(shared_pending>NX2_SHARED_TRACE_SLOTS){
        row(crash,"SHARED_HISTORY_GAP lost=%u\n",shared_pending-NX2_SHARED_TRACE_SLOTS);
        diagnostic_shared_cursor=shared_next-NX2_SHARED_TRACE_SLOTS;
      }
      for(unsigned n=0;n<NX2_SHARED_TRACE_SLOTS && diagnostic_shared_cursor!=shared_next;n++){
        uint32_t ticket=diagnostic_shared_cursor;
        const Nx2SharedEvent *e=&l->shared_trace[ticket%NX2_SHARED_TRACE_SLOTS];
        uint32_t seq=__atomic_load_n(&e->sequence,__ATOMIC_ACQUIRE);
        if(seq!=ticket+1)break;
        Nx2SharedEvent v={0};
#define SHARED_READ(f) v.f=__atomic_load_n(&e->f,__ATOMIC_RELAXED)
        SHARED_READ(id);SHARED_READ(operation);SHARED_READ(phase);SHARED_READ(handle);
        SHARED_READ(permission);SHARED_READ(result);SHARED_READ(tick);SHARED_READ(tls);
        SHARED_READ(caller);SHARED_READ(address);SHARED_READ(bytes);SHARED_READ(owner);
#undef SHARED_READ
        if(__atomic_load_n(&e->sequence,__ATOMIC_ACQUIRE)!=seq)break;
        char owner[9]={0};
        for(unsigned k=0;k<8;k++)owner[k]=(char)(v.owner>>(8*k));
        if(v.phase==2){
          if(v.result)diagnostic_memory_failed++;
          else diagnostic_memory_ok++;
        }
        int keep_memory=!compact_gameplay || v.operation==1 ||
            (v.phase==2 && v.result) || shared_next-ticket<=16;
        if(!keep_memory)diagnostic_memory_omitted++;
        if(keep_memory)row(crash,"SHARED_MEMORY_EVENT seq=%u id=%u op=%u phase=%u handle=%08x owner=%s address=%016llx bytes=%llu perm=%x result=%08x tick=%llu tls=%016llx caller=%016llx\n",
            seq,v.id,v.operation,v.phase,v.handle,owner,
            (unsigned long long)v.address,(unsigned long long)v.bytes,v.permission,v.result,
            (unsigned long long)v.tick,(unsigned long long)v.tls,(unsigned long long)v.caller);
        if(v.phase==2 && !v.result && (v.operation==1 || v.operation==14) && v.bytes){
          MemoryInfo mi={0},last={0};u32 page=0;
          Result first_rc=svcQueryMemory(&mi,&page,v.address);
          int range_ok=v.bytes-1<=UINT64_MAX-v.address;
          Result last_rc=range_ok?svcQueryMemory(&last,&page,v.address+v.bytes-1):first_rc;
          row(crash,"MEMORY_METADATA_SAMPLE id=%u first_result=%08x last_result=%08x range_ok=%u first_type=%u last_type=%u first_perm=%u last_perm=%u requested_perm=%u\n",
              v.id,first_rc,last_rc,(unsigned)range_ok,mi.type,last.type,mi.perm,last.perm,v.permission);
        }
        diagnostic_shared_cursor++;
      }
      /* Export the observation tick: recorded map returns and later snapshots
       * are not simultaneous. Offline analysis must not treat them as such. */
      row(crash,"MEMORY_LOG_SUMMARY compact=%u observed_ok=%llu observed_failed=%llu omitted_records=%llu\n",
          (unsigned)compact_gameplay,(unsigned long long)diagnostic_memory_ok,
          (unsigned long long)diagnostic_memory_failed,(unsigned long long)diagnostic_memory_omitted);
      row(crash,"MEMORY_OBSERVATION_EPOCH tick=%llu consumed=%u produced=%u\n",
          (unsigned long long)armGetSystemTick(),diagnostic_shared_cursor,shared_next);
      row(crash,"SHARED_HISTORY_PROGRESS produced=%u consumed=%u pending=%u\n",
          shared_next,diagnostic_shared_cursor,shared_next-diagnostic_shared_cursor);
      uint64_t physical_total=0,physical_used=0;
      Result total_rc=svcGetInfo(&physical_total,6,CUR_PROCESS_HANDLE,0);
      Result used_rc=svcGetInfo(&physical_used,7,CUR_PROCESS_HANDLE,0);
      uint64_t total_after=0;
      Result total_after_rc=svcGetInfo(&total_after,6,CUR_PROCESS_HANDLE,0);
      row(crash,"PHYSICAL_ACCOUNTING_CHECK total_before=%llu total_after=%llu result=%08x consistent=%u\n",
          (unsigned long long)physical_total,(unsigned long long)total_after,total_after_rc,
          (unsigned)(R_SUCCEEDED(total_rc)&&R_SUCCEEDED(used_rc)&&R_SUCCEEDED(total_after_rc)&&physical_total==total_after&&physical_used<=physical_total));
      row(crash,"ACCOUNTING_OBSERVATION tick=%llu total_rc=%08x used_rc=%08x after_rc=%08x stable_total=%u used_within_total=%u\n",
          (unsigned long long)armGetSystemTick(),total_rc,used_rc,total_after_rc,
          (unsigned)(R_SUCCEEDED(total_rc)&&R_SUCCEEDED(total_after_rc)&&physical_total==total_after),
          (unsigned)(R_SUCCEEDED(total_rc)&&R_SUCCEEDED(used_rc)&&physical_used<=physical_total));
      row(crash,"PROCESS_PHYSICAL_SNAPSHOT total_result=%08x used_result=%08x total=%llu used=%llu free=%llu\n",
          total_rc,used_rc,(unsigned long long)physical_total,
          (unsigned long long)physical_used,
          (unsigned long long)(R_SUCCEEDED(total_rc)&&R_SUCCEEDED(used_rc)&&physical_total>=physical_used?physical_total-physical_used:0));
      unsigned pending=0;
      for(unsigned i=0;i<NX2_RPC_SLOTS;i++)
        if(__atomic_load_n(&l->requests[i].state,__ATOMIC_ACQUIRE))
          pending++;
      {
        uint32_t io_slot=__atomic_load_n(&host->rpc_active_slot,__ATOMIC_ACQUIRE);
        uint64_t io_started=__atomic_load_n(&host->rpc_started_tick,__ATOMIC_RELAXED);
        uint64_t now_tick=armGetSystemTick();
        uint64_t io_age_ms=io_slot && now_tick>=io_started
            ?armTicksToNs(now_tick-io_started)/1000000ULL:0;
        row(crash,"STORAGE_SERVICE_SNAPSHOT active_slot=%u op=%u age_ms=%llu completed=%llu thread_running=%u\n",
            io_slot,__atomic_load_n(&host->rpc_active_op,__ATOMIC_RELAXED),
            (unsigned long long)io_age_ms,
            (unsigned long long)__atomic_load_n(&host->rpc_completed_count,__ATOMIC_RELAXED),
            __atomic_load_n(&nx2_storage_service.running,__ATOMIC_ACQUIRE));
        if(io_age_ms>diagnostic_max_io_age)diagnostic_max_io_age=io_age_ms;
        uint64_t poll_tick=__atomic_load_n(&nx2_storage_service.last_poll_tick,__ATOMIC_ACQUIRE);
        row(crash,"STORAGE_LATENCY_SNAPSHOT max_observed_active_ms=%llu last_poll_ms=%llu\n",
            (unsigned long long)diagnostic_max_io_age,
            (unsigned long long)(poll_tick && now_tick>=poll_tick?armTicksToNs(now_tick-poll_tick)/1000000ULL:0));
        if(io_slot && io_age_ms>=1500){
          Nx2NupReadProgress p={0};
          nx2_nup_get_progress(&p);
          if(p.active)
            row(crash,"NUP_STORAGE_PROGRESS file=%u codec=%u phase=%u request=%llu sd_bytes=%llu decoded=%llu delivered=%llu active_ms=%llu\n",
                p.file,p.codec,p.phase,
                (unsigned long long)p.requested,
                (unsigned long long)p.sd_bytes,
                (unsigned long long)p.decoded_bytes,
                (unsigned long long)p.delivered_bytes,
                (unsigned long long)io_age_ms);
        }
      }
      row(crash,"GPU_OBSERVATION_MODE mode=%u trace=%u zero_counters_not_proof_of_no_gpu_activity=%u\n",
          (unsigned)l->nvn_driver_passthrough,(unsigned)l->nvn_gameplay_trace,
          (unsigned)(l->nvn_driver_passthrough==2));
      row(crash,"GPU_INIT_SNAPSHOT trace_enabled=%u device=%u/%u result=%08x queue=%u/%u result=%08x window=%u/%u result=%08x\n",
          (unsigned)l->nvn_gameplay_trace,
          __atomic_load_n(&l->nvn_device_entered,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_device_returned,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_device_result,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_queue_entered,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_queue_returned,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_queue_result,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_window_entered,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_window_returned,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_window_result,__ATOMIC_ACQUIRE));
      if(l->nvn_gameplay_trace){
        for(unsigned n=0;n<NX2_NVN_STAGE_COUNT;n++){
          row(crash,"GPU_BOUNDARY_SNAPSHOT stage=%u entered=%u returned=%u result=%08x caller=%016llx thread_tls=%016llx enter_tick=%llu return_tick=%llu\n",
              n,
              __atomic_load_n(&l->nvn_stage_entered[n],__ATOMIC_ACQUIRE),
              __atomic_load_n(&l->nvn_stage_returned[n],__ATOMIC_ACQUIRE),
              __atomic_load_n(&l->nvn_stage_result[n],__ATOMIC_ACQUIRE),
              (unsigned long long)__atomic_load_n(&l->nvn_stage_last_caller[n],__ATOMIC_ACQUIRE),
              (unsigned long long)__atomic_load_n(&l->nvn_stage_last_tls[n],__ATOMIC_ACQUIRE),
              (unsigned long long)__atomic_load_n(&l->nvn_stage_last_tick[n],__ATOMIC_ACQUIRE),
              (unsigned long long)__atomic_load_n(&l->nvn_stage_return_tick[n],__ATOMIC_ACQUIRE));
        }
      }
      if(l->nvn_gameplay_trace){
        uint32_t produced=__atomic_load_n(&l->gpu_boundary_next,__ATOMIC_ACQUIRE);
        uint32_t available=produced-diagnostic_gpu_cursor;
        if(available>NX2_GPU_BOUNDARY_RING_SLOTS){
          row(crash,"GPU_HISTORY_GAP lost=%u\n",available-NX2_GPU_BOUNDARY_RING_SLOTS);
          diagnostic_gpu_cursor=produced-NX2_GPU_BOUNDARY_RING_SLOTS;
        }
        /* Limit per-snapshot output; retain cursor for the next sample. */
        for(unsigned n=0;n<NX2_GPU_BOUNDARY_RING_SLOTS && diagnostic_gpu_cursor!=produced;n++){
          uint32_t ticket=diagnostic_gpu_cursor;
          const Nx2GpuBoundary *source=&l->gpu_boundary[ticket%NX2_GPU_BOUNDARY_RING_SLOTS];
          uint32_t seq=__atomic_load_n(&source->sequence,__ATOMIC_ACQUIRE);
          if(seq!=ticket+1)break;
          Nx2GpuBoundary event=*source;
          if(__atomic_load_n(&source->sequence,__ATOMIC_ACQUIRE)!=seq)break;
          int keep_gpu=!compact_gameplay || produced-ticket<=16;
          if(!keep_gpu)diagnostic_gpu_omitted++;
          if(keep_gpu)row(crash,"GPU_STARTUP_EVENT seq=%u kind=%u tick=%llu tls=%016llx caller=%016llx queue=%016llx window=%016llx texture=%d detail=%016llx\n",
              seq,event.kind,(unsigned long long)event.tick,(unsigned long long)event.thread_tls,
              (unsigned long long)event.caller,(unsigned long long)event.queue,
              (unsigned long long)event.window,event.texture_index,(unsigned long long)event.detail);
          diagnostic_gpu_cursor++;
        }
        row(crash,"GPU_LOG_SUMMARY compact=%u omitted_records=%llu\n",
            (unsigned)compact_gameplay,(unsigned long long)diagnostic_gpu_omitted);
        row(crash,"GPU_HISTORY_PROGRESS produced=%u consumed=%u pending=%u\n",produced,diagnostic_gpu_cursor,produced-diagnostic_gpu_cursor);
      }
      row(crash,"GUEST_FSP_ROUTE_SNAPSHOT stage=%u cmd=%u object=%08x result=%08x child=%08x\n",
          __atomic_load_n(&l->fsp_route_stage,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->fsp_route_command,__ATOMIC_RELAXED),
          __atomic_load_n(&l->fsp_route_object,__ATOMIC_RELAXED),
          __atomic_load_n(&l->fsp_route_result,__ATOMIC_RELAXED),
          __atomic_load_n(&l->fsp_route_child,__ATOMIC_RELAXED));
      row(crash,"HOST_SNAPSHOT elapsed_ms=%llu stage=%u fault_state=%u guest_main=%llu rpc=%llu rom_bytes=%llu pending=%u guest_abort=%u host_abort=%u frames=%llu nvn_bootstrap=%u getproc=%u queues=%u/%u window=%u/%u submit=%u/%u finish=%u/%u acquire=%u/%u present=%u/%u ipc=%u/%u\n",
          (unsigned long long)(armTicksToNs(crash_sample-began)/1000000ULL),
          stage,__atomic_load_n(&nx2_fault.state,__ATOMIC_ACQUIRE),
          (unsigned long long)__atomic_load_n(&l->main_entered,__ATOMIC_ACQUIRE),
          (unsigned long long)host->calls,(unsigned long long)host->bytes,
          pending,__atomic_load_n(&l->abort_requested,__ATOMIC_ACQUIRE),
          host->abort,(unsigned long long)frames,
          __atomic_load_n(&l->nvn_bootstrap_calls,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_getproc_calls,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_queue_entered,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_queue_returned,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_window_entered,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_window_returned,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_stage_entered[0],__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_stage_returned[0],__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_stage_entered[2],__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_stage_returned[2],__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_stage_entered[3],__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_stage_returned[3],__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_present_entered,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_present_returned,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->ipc_trace_begins,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->ipc_trace_ends,__ATOMIC_ACQUIRE));
    }
    /* Keep full IPC/RPC history in RAM; avoid rewriting identical trace
     * tails every 2 seconds while a game is actively rendering. */
    int detailed_crash_snapshot=0;
    if(crash && captured_snapshot &&
       (!crash_detail_written ||
        armTicksToNs(armGetSystemTick()-crash_detail_tick)>=
            (frames?6000000000ULL:2000000000ULL))){
      detailed_crash_snapshot=1;
      crash_detail_written=1;
      crash_detail_tick=armGetSystemTick();
    }
    /* Include recent IPC requests in the independent crash report even for
     * untagged services such as SSL, CSRNG and NSD. No user buffers are
     * captured; only protocol metadata, caller and return status. */
    if(crash && detailed_crash_snapshot){
      uint32_t next=__atomic_load_n(&l->ipc_trace_next,__ATOMIC_ACQUIRE);
      if(next!=previous_crash_ipc_next){
        uint32_t count=next<8?next:8;
        for(uint32_t back=count;back;back--){
          uint32_t slot=(next-back)%NX2_IPC_TRACE_SLOTS;
          Nx2IpcBreadcrumb *e=&l->ipc_breadcrumbs[slot];
          uint32_t phase=__atomic_load_n(&e->phase,__ATOMIC_ACQUIRE);
          if(!phase)continue;
          Nx2IpcBreadcrumb v=*e;
          if(__atomic_load_n(&e->phase,__ATOMIC_ACQUIRE)!=phase)continue;
          v.service[8]=0;
          int module=-1;
          uint64_t offset=0;
          for(unsigned k=0;k<images->count;k++){
            const Nx2Module *m=&images->modules[k];
            uint64_t base=(uintptr_t)m->guest;
            uint64_t length=m->total>=4096?m->total-4096:0;
            if(v.caller>=base && v.caller-base<length){
              module=(int)k;offset=v.caller-base;break;
            }
          }
          row(crash,"IPC_CRASH_TAIL slot=%u phase=%u owner=%s handle=%08llx type=%u command=%u kernel=%08x cmif=%08x caller=%016llx module=%d offset=%llx\n",
            slot,phase,v.service,(unsigned long long)v.handle,
            v.request_type,v.command,v.result,v.service_result,
            (unsigned long long)v.caller,module,
            (unsigned long long)offset);
        }
        previous_crash_ipc_next=next;
      }
    }
    /* Independent crash report retains recent detailed requests and replies,
     * even when the full gameplay/NUP trace cannot be completely drained. */
    if(crash && detailed_crash_snapshot){
      uint32_t total=__atomic_load_n(&l->ipc_event_next,__ATOMIC_ACQUIRE);
      uint32_t count=total<24?total:24;
      for(uint32_t back=count;back;back--){
        uint32_t expected=total-back+1;
        Nx2IpcEvent *entry=&l->ipc_events[(expected-1)%NX2_IPC_EVENT_SLOTS];
        if(__atomic_load_n(&entry->sequence,__ATOMIC_ACQUIRE)!=expected)
          continue;
        Nx2IpcEvent v=*entry;
        if(__atomic_load_n(&entry->sequence,__ATOMIC_ACQUIRE)!=expected)
          continue;
        v.service[8]=0;
        row(crash,"IPC_EVENT_TAIL seq=%u call=%u phase=%u owner=%s kind=%u handle=%08x hipc=%u command=%u object=%08x flags=%02x kernel=%08x cmif=%08x caller=%016llx\n",
            expected,v.call_id,v.phase,v.service,v.kind,v.handle,
            v.request_type,v.command,v.object_id,v.flags,
            v.kernel_result,v.cmif_result,(unsigned long long)v.caller);
      }
    }
    /* Guest-to-host RPC callsites are recorded before IPC, independently of
     * filesystem logging. Keep a short tail in the separate crash file;
     * correlate guest module offsets even if the last event has no reply. */
    if(crash && detailed_crash_snapshot){
      uint32_t next=__atomic_load_n(&l->rpc_trace_next,__ATOMIC_ACQUIRE);
      uint32_t count=next<4?next:4;
      for(uint32_t back=count;back;back--){
        uint32_t ticket=next-back+1;
        Nx2RpcTrace *t=&l->rpc_trace[(ticket-1)%NX2_RPC_TRACE_SLOTS];
        if(__atomic_load_n(&t->sequence,__ATOMIC_ACQUIRE)!=ticket)continue;
        uint64_t caller=__atomic_load_n(&t->caller,__ATOMIC_RELAXED);
        uint64_t thread=__atomic_load_n(&t->thread,__ATOMIC_RELAXED);
        uint32_t op=__atomic_load_n(&t->op,__ATOMIC_RELAXED);
        uint32_t phase=__atomic_load_n(&t->phase,__ATOMIC_ACQUIRE);
        uint32_t result=__atomic_load_n(&t->result,__ATOMIC_RELAXED);
        if(__atomic_load_n(&t->sequence,__ATOMIC_ACQUIRE)!=ticket)continue;
        int module=-1;
        uint64_t offset=0;
        for(unsigned k=0;k<images->count;k++){
          const Nx2Module *m=&images->modules[k];
          uint64_t base=(uintptr_t)m->guest;
          uint64_t length=m->total>=4096?m->total-4096:0;
          if(caller>=base && caller-base<length){
            module=(int)k;offset=caller-base;break;
          }
        }
        row(crash,"RPC_TRAIL ticket=%u op=%u phase=%u result=%08x thread=%016llx caller=%016llx module=%d module_offset=%llx\n",
            ticket,op,phase,result,(unsigned long long)thread,
            (unsigned long long)caller,module,(unsigned long long)offset);
      }
    }
    /* Record real C++ guest callsites instead of generic RPC bridge callers.
     * A nonzero entry/return gap may be normal while concurrent reads run. */
    if(crash && detailed_crash_snapshot){
      uint64_t sites[2]={
        __atomic_load_n(&l->fs_guest_read_caller,__ATOMIC_ACQUIRE),
        __atomic_load_n(&l->save_gate_caller,__ATOMIC_ACQUIRE)};
      const char *site_names[2]={"rom_storage_read","save_mount"};
      for(unsigned t=0;t<2;t++){
        int module=-1; uint64_t offset=0;
        for(unsigned j=0;j<images->count;j++){
          const Nx2Module *m=&images->modules[j];
          uint64_t base=(uintptr_t)m->guest;
          uint64_t length=m->total>=4096?m->total-4096:0;
          if(sites[t]>=base && sites[t]-base<length){
            module=(int)j;offset=sites[t]-base;break;
          }
        }
        row(crash,"GUEST_CALLSITE name=%s caller=%016llx module=%d offset=%llx\n",
            site_names[t],(unsigned long long)sites[t],module,
            (unsigned long long)offset);
      }
      row(crash,"GUEST_FS_READ entries=%u returned=%u last_offset=%016llx last_size=%llu last_result=%08x\n",
          __atomic_load_n(&l->fs_guest_read_entries,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->fs_guest_read_returns,__ATOMIC_ACQUIRE),
          (unsigned long long)__atomic_load_n(&l->fs_guest_read_offset,__ATOMIC_ACQUIRE),
          (unsigned long long)__atomic_load_n(&l->fs_guest_read_size,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->fs_guest_read_result,__ATOMIC_ACQUIRE));
      row(crash,"GUEST_SAVE_GATE phase=%u result=%08x guest_main_returned=%u\n",
          __atomic_load_n(&l->save_gate_phase,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->save_gate_result,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->guest_main_returned,__ATOMIC_ACQUIRE));
    }
    uint32_t save_phase=__atomic_load_n(&l->save_gate_phase,__ATOMIC_ACQUIRE);
    if(save_phase!=last_save_gate_phase){
      row(f,"SAVE_GATE_PROGRESS phase=%u result=%08x caller=%016llx\n",
          save_phase,__atomic_load_n(&l->save_gate_result,__ATOMIC_ACQUIRE),
          (unsigned long long)__atomic_load_n(&l->save_gate_caller,__ATOMIC_ACQUIRE));
      if(crash)row(crash,"SAVE_GATE_PROGRESS phase=%u result=%08x caller=%016llx\n",
          save_phase,__atomic_load_n(&l->save_gate_result,__ATOMIC_ACQUIRE),
          (unsigned long long)__atomic_load_n(&l->save_gate_caller,__ATOMIC_ACQUIRE));
      last_save_gate_phase=save_phase;
    }
    /* One filesystem flush per completed crash sample, not one per entry.
     * A sudden kernel kill can still truncate the current sample; the
     * previous completed sample is flushed and remains usable. */
    if(crash && captured_snapshot) fflush(crash);
    /* Capture recent guest SVCs from shared memory without generating
     * any guest RPCs. This survives a guest thread stuck inside the SVC.
     * A short interval is used only after private-save startup. */
    if(__atomic_load_n(&l->late_trace_active,__ATOMIC_ACQUIRE) &&
       armTicksToNs(armGetSystemTick()-trace_sample)>=250000000ULL){
      trace_sample=armGetSystemTick();
      uint32_t next=__atomic_load_n(&l->ipc_trace_next,__ATOMIC_ACQUIRE);
      uint32_t ended=__atomic_load_n(&l->ipc_trace_ends,__ATOMIC_ACQUIRE);
      uint32_t started=__atomic_load_n(&l->ipc_trace_begins,__ATOMIC_ACQUIRE);
      if((ended!=previous_trace_end || started!=previous_trace_begin) &&
         (!frames || started>ended)){
        row(f,"IPC_WATCHDOG started=%u ended=%u in_flight_estimate=%u next=%u frames=%llu\n",
            started,ended,started>=ended?started-ended:0,next,
            (unsigned long long)frames);
        uint32_t count=next<8?next:8;
        for(uint32_t back=count;back;back--){
          uint32_t slot=(next-back)%NX2_IPC_TRACE_SLOTS;
          Nx2IpcBreadcrumb *e=&l->ipc_breadcrumbs[slot];
          uint32_t phase=__atomic_load_n(&e->phase,__ATOMIC_ACQUIRE);
          if(phase)
            row(f,"IPC_WATCHDOG_CALL slot=%u phase=%u service=%s handle=%08llx type=%u command=%u caller=%016llx kernel=%08x cmif=%08x\n",
                slot,phase,e->service,(unsigned long long)e->handle,
                e->request_type,e->command,(unsigned long long)e->caller,
                e->result,__atomic_load_n(&e->service_result,__ATOMIC_ACQUIRE));
        }
        previous_trace_end=ended;
        previous_trace_begin=started;
      }
    }
    /* A post-save checkpoint is independent of guest RPC progress;
     * use shorter heartbeats only after the save mount succeeds. */
    uint32_t late=__atomic_load_n(&l->late_trace_active,__ATOMIC_ACQUIRE);
    if (armTicksToNs(armGetSystemTick() - heartbeat) >=
        (late?2000000000ULL:5000000000ULL)) {
      heartbeat = armGetSystemTick();
      {
        uint64_t io_last=__atomic_load_n(&nx2_storage_service.last_poll_tick,__ATOMIC_ACQUIRE);
        uint64_t io_now=armGetSystemTick();
        uint32_t io_slot=__atomic_load_n(&host->rpc_active_slot,__ATOMIC_ACQUIRE);
        uint64_t io_started=__atomic_load_n(&host->rpc_started_tick,__ATOMIC_RELAXED);
        row(f,"STORAGE_SERVICE_HEARTBEAT running=%u active_slot=%u active_op=%u active_ms=%llu completed=%llu last_poll_ms=%llu\n",
            __atomic_load_n(&nx2_storage_service.running,__ATOMIC_ACQUIRE),
            io_slot,__atomic_load_n(&host->rpc_active_op,__ATOMIC_RELAXED),
            (unsigned long long)(io_slot&&io_now>=io_started?armTicksToNs(io_now-io_started)/1000000ULL:0),
            (unsigned long long)__atomic_load_n(&host->rpc_completed_count,__ATOMIC_RELAXED),
            (unsigned long long)(io_now>=io_last?armTicksToNs(io_now-io_last)/1000000ULL:0));
      }
      row(f,"IPC_CAPTURE_SUMMARY observed_events=%u consumed_events=%u dropped=%u full_text=%u\n",
          __atomic_load_n(&l->ipc_event_next,__ATOMIC_ACQUIRE),
          ipc_event_cursor,ipc_event_dropped,(unsigned)ipc_full_text);
      row(f,
          "LAUNCH_HEARTBEAT elapsed_seconds=%llu main_entered=%llu "
          "rpc_calls=%llu rom_bytes=%llu frames=%llu\n",
          (unsigned long long)(armTicksToNs(heartbeat - began) / 1000000000ULL),
          (unsigned long long)__atomic_load_n(&l->main_entered,
                                              __ATOMIC_ACQUIRE),
          (unsigned long long)host->calls, (unsigned long long)host->bytes,
          (unsigned long long)frames);
      unsigned pending=0;
      for(unsigned slot=0;slot<NX2_RPC_SLOTS;slot++){
        Nx2Request *request=&l->requests[slot];
        uint32_t state=__atomic_load_n(&request->state,__ATOMIC_ACQUIRE);
        if(state){
          pending++;
          /* State 1 is only claimed, so op/args are not initialized yet.
           * State 2 is published and safely readable via acquire. */
          if(state==2 || state==3)
            row(f,"RPC_PENDING slot=%u state=%u op=%u a=%016llx b=%016llx c=%016llx\n",
                slot,state,request->op,
                (unsigned long long)request->a,
                (unsigned long long)request->b,
                (unsigned long long)request->c);
          else
            row(f,"RPC_PENDING slot=%u state=%u unpublished=1\n",slot,state);
        }
      }
      row(f,"RPC_HEARTBEAT_SUMMARY pending=%u host_abort=%u guest_abort=%u stage=%u late_trace=%u\n",
          pending,host->abort,
          __atomic_load_n(&l->abort_requested,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->stage,__ATOMIC_ACQUIRE),late);
      if(l->nvn_gameplay_trace || l->nvn_driver_passthrough==0){
        for(unsigned n=0;n<NX2_NVN_STAGE_COUNT;n++)
          row(f,"GPU_BOUNDARY stage=%u entered=%u returned=%u caller=%016llx thread_tls=%016llx\n",
              n,
              __atomic_load_n(&l->nvn_stage_entered[n],__ATOMIC_ACQUIRE),
              __atomic_load_n(&l->nvn_stage_returned[n],__ATOMIC_ACQUIRE),
              (unsigned long long)__atomic_load_n(&l->nvn_stage_last_caller[n],__ATOMIC_ACQUIRE),
              (unsigned long long)__atomic_load_n(&l->nvn_stage_last_tls[n],__ATOMIC_ACQUIRE));
      }
      row(f,"GRAPHICS_PATH bootstrap=%u getproc=%u present_lookups=%u present_entered=%u present_returned=%u\n",
          __atomic_load_n(&l->nvn_bootstrap_calls,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_getproc_calls,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_present_lookups,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_present_entered,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_present_returned,__ATOMIC_ACQUIRE));
      row(f,"NVN_INIT device=%u/%u result=%08x queue=%u/%u result=%08x window=%u/%u result=%08x\n",
          __atomic_load_n(&l->nvn_device_entered,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_device_returned,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_device_result,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_queue_entered,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_queue_returned,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_queue_result,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_window_entered,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_window_returned,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_window_result,__ATOMIC_ACQUIRE));
      {
        static const char *names[]={"submit","flush","finish","window_acquire","queue_acquire"};
        for(unsigned n=0;n<NX2_NVN_STAGE_COUNT;n++)
          row(f,"NVN_STAGE name=%s entered=%u returned=%u latest_result=%08x\n",
              names[n],
              __atomic_load_n(&l->nvn_stage_entered[n],__ATOMIC_ACQUIRE),
              __atomic_load_n(&l->nvn_stage_returned[n],__ATOMIC_ACQUIRE),
              __atomic_load_n(&l->nvn_stage_result[n],__ATOMIC_ACQUIRE));
      }
      /* Compare against emulator's documented NVDRV commands (0 Open,
       * 3 Initialize) and VI layer/display commands (1010, 2020, 2030,
       * 5202) without emulating their replies or changing the native SVC. */
      uint32_t gnext=__atomic_load_n(&l->graphics_ipc_next,__ATOMIC_ACQUIRE);
      if(gnext!=previous_graphics_ipc){
        row(f,"GRAPHICS_SERVICE_HISTORY total=%u previous=%u\n",
            gnext,previous_graphics_ipc);
        uint32_t gcount=gnext<16?gnext:16;
        for(uint32_t back=gcount;back;back--){
          uint32_t slot=(gnext-back)%NX2_GRAPHICS_TRACE_SLOTS;
          Nx2IpcBreadcrumb *e=&l->graphics_ipc[slot];
          uint32_t phase=__atomic_load_n(&e->phase,__ATOMIC_ACQUIRE);
          if(!phase)continue;
          const char *meaning="";
          if(e->service[0]=='n'&&e->service[1]=='v'){
            if(e->command==0)meaning="NVDRV_OPEN";
            else if(e->command==3)meaning="NVDRV_INITIALIZE";
            else if(e->command==1||e->command==11||e->command==12)
              meaning="NVDRV_IOCTL";
          }else if(e->service[0]=='v'&&e->service[1]=='i'){
            if(e->command==1010||e->command==1011)meaning="VI_OPEN_DISPLAY";
            else if(e->command==2020||e->command==2030)
              meaning="VI_CREATE_OR_OPEN_LAYER";
            else if(e->command==5202)meaning="VI_GET_VSYNC";
          }
          row(f,"GRAPHICS_SERVICE_CALL phase=%u service=%s command=%u hint=%s kernel=%08x cmif=%08x handle=%08llx caller=%016llx\n",
              phase,e->service,e->command,meaning,e->result,
              __atomic_load_n(&e->service_result,__ATOMIC_ACQUIRE),
              (unsigned long long)e->handle,(unsigned long long)e->caller);
        }
        previous_graphics_ipc=gnext;
      }
    }
    if (!__atomic_load_n(&l->main_entered, __ATOMIC_ACQUIRE) &&
        armTicksToNs(armGetSystemTick() - began) > 300000000000ULL)
      stop(f, "Startup exceeded five minutes before main entry");
    s32 index = -1;
    rc = svcWaitSynchronization(&index, &worker.handle, 1, 0);
    if (R_SUCCEEDED(rc))
      stop(f, "Game worker returned unexpectedly");
    if (rc != KERNELRESULT(TimedOut))
      stop(f, "Game worker wait failed");
    svcSleepThread(1000000);
  }
done:
  row(f, "REPORT_END game_entry_attempted=0 phase=%s\n",launch_phase);
  if(crash)row(crash,"PRELAUNCH_STOP phase=%s report_end=1\n",launch_phase);
  fclose(f);
  if(crash)fclose(crash);
  nx2_nup_close();
  free(map);
  free(l);
  free(host);
  free(images);
}
/* The native CodeFS preflight is intentionally read-only. It tests the
 * actual HEN fsp-ldr root and a known-running host title before the selected
 * guest. No game process is started; no Loader global identity is changed.
 * This remains separate from legacy A and B+A and writes its own report. */
static Result horizon_probe_codefs(FILE *report,Service *loader,
                                   const char *role,u64 title){
  const u8 stores[]={
    NcmStorageId_BuiltInUser,NcmStorageId_SdCard,NcmStorageId_GameCard
  };
  Result last=0x2f5e02;
  for(unsigned i=0;i<sizeof(stores)/sizeof(stores[0]);i++){
    const struct {
      u8 attributes,storage_id,padding[6];
      u64 program_id;
    } request={1,stores[i],{0},title};
    unsigned char code_info[0x1a4]={0};
    FsFileSystem codefs={0};
    Result rc=serviceDispatchIn(loader,0,request,
       .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_Out},
       .buffers={{code_info,sizeof(code_info)}},
       .out_num_objects=1,.out_objects=&codefs.s);
    row(report,"CODEFS role=%s title=%016llx storage=%u result=%08x\n",
        role,(unsigned long long)title,(unsigned)stores[i],rc);
    if(R_SUCCEEDED(rc)){
      fsFsClose(&codefs);
      return 0;
    }
    last=rc;
  }
  return last;
}
static void horizon_preflight(const Nx2Game *game){
  FILE *report=fopen("sdmc:/BackupLoaderMKW-horizon.log","w");
  if(!report){
    snprintf(outcome,sizeof(outcome),"R+A: cannot open Horizon preflight log");
    return;
  }
  row(report,"HORIZON_PREFLIGHT version=1 source=%s game=%s title=%016llx\n"
      "MODE=read_only_codefs_probe NOT_A_GAME_LAUNCH=1\n",
      game->is_nup?"nup":"folder",game->name,(unsigned long long)game->id);
  u64 host=0;
  Result rc=svcGetInfo(&host,InfoType_ProgramId,CUR_PROCESS_HANDLE,0);
  row(report,"HOST_ID result=%08x title=%016llx\n",rc,(unsigned long long)host);
  if(R_FAILED(rc))goto failed;
  Service ro={0},hen={0};
  rc=kaihenGetRoSession(&ro);
  row(report,"RO_SESSION result=%08x\n",rc);
  if(R_FAILED(rc))goto failed;
  rc=kaihenOpenHomebrewSession(&ro,&hen);
  row(report,"HEN_SESSION result=%08x\n",rc);
  if(R_FAILED(rc))goto failed;
  Handle loader_handle=INVALID_HANDLE;
  rc=henGetLoaderRoot(&hen,&loader_handle);
  row(report,"HEN_GET_FSP_LDR_ROOT result=%08x\n",rc);
  serviceClose(&hen);
  if(R_FAILED(rc))goto failed;
  Service loader={0};
  serviceCreate(&loader,loader_handle);
  Result host_rc=horizon_probe_codefs(report,&loader,"host",host);
  Result guest_rc=horizon_probe_codefs(report,&loader,"selected",game->id);
  serviceClose(&loader);
  row(report,"RESULT host_codefs=%08x selected_codefs=%08x\n",
      host_rc,guest_rc);
  if(R_FAILED(host_rc)){
    snprintf(outcome,sizeof(outcome),
      "R+A: Horizon CodeFS unavailable for host (%08x); see horizon.log",host_rc);
  }else if(R_FAILED(guest_rc)){
    snprintf(outcome,sizeof(outcome),
      "R+A: host CodeFS works; selected title not available (%08x)",guest_rc);
  }else{
    snprintf(outcome,sizeof(outcome),
      "R+A: selected title CodeFS available; native guest launch NOT implemented");
  }
  fclose(report);
  return;
failed:
  row(report,"RESULT native_probe_unavailable=%08x\n",rc);
  snprintf(outcome,sizeof(outcome),"R+A: Horizon probe unavailable (%08x)",rc);
  fclose(report);
}
static void draw(const Nx2Catalog *catalog, size_t selected) {
  consoleClear();
  printf("BackupLoaderMKW 0.14\n\n");
  if (!catalog->count)
    printf("No extracted games or NUP backups found in /games.\n\n");
  else {
    size_t page = (selected / 15) * 15;
    for (size_t i = page; i < catalog->count && i < page + 15; i++)
      printf("%s %.66s\n", i == selected ? ">" : " ", catalog->items[i].name);
    printf("\n%u / %u\n", (unsigned)selected + 1, (unsigned)catalog->count);
    if (catalog->items[selected].is_nup)
      printf("NUP: %s\n", catalog->items[selected].nup_status);
    else if (catalog->items[selected].updates==2)
      printf("Using complete update version %llu (SD-only).\n",
             (unsigned long long)catalog->items[selected].update_version);
    else if (catalog->items[selected].updates)
      printf("Base selected; no complete runnable update found.\n");
  }
  printf("\nA: native + present   L+A: full native   ZR+A: debug NVN\n"
         "B+A: rebuild index   R+A: Horizon check\n"
         "X: refresh   +: exit\n\n%s\n",
         outcome);
  consoleUpdate(NULL);
}
/* Stage two: service connection and read-only enumeration diagnostics. */
int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  consoleInit(NULL);
  FILE *monitor_flag=fopen("sdmc:/kaihen/nx2-monitor.flag","rb");
  if(monitor_flag){monitor_enabled=1;fclose(monitor_flag);monitor_emit(0,0,0,0,1);}
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad);
  Nx2Catalog catalog = {0};
  nx2_catalog("sdmc:/games", &catalog);
  size_t selected = 0;
  u64 repeat = 0;
  int direction = 0;
  while (appletMainLoop()) {
    monitor_emit(0,0,0,0,0);
    padUpdate(&pad);
    u64 down = padGetButtonsDown(&pad), held = padGetButtons(&pad);
    if (down & HidNpadButton_Plus)
      break;
    if (down & HidNpadButton_X) {
      nx2_catalog_free(&catalog);
      nx2_catalog("sdmc:/games", &catalog);
      selected = 0;
    }
    u64 up = HidNpadButton_Up | HidNpadButton_StickLUp,
        dn = HidNpadButton_Down | HidNpadButton_StickLDown;
    int next = (held & up) ? -1 : (held & dn) ? 1 : 0;
    u64 tick = armGetSystemTick();
    int move = 0;
    if (next && ((down & (up | dn)) || next != direction)) {
      move = next;
      repeat = tick + armNsToTicks(350000000);
    } else if (next && tick >= repeat) {
      move = next;
      repeat = tick + armNsToTicks(100000000);
    }
    direction = next;
    if (catalog.count && move)
      selected = move < 0 ? (selected ? selected - 1 : catalog.count - 1)
                          : (selected + 1) % catalog.count;
    if (catalog.count && (down & HidNpadButton_A)) {
      const Nx2Game *game=&catalog.items[selected];
      if(held & HidNpadButton_R){
        horizon_preflight(game);
      }else if (game->is_nup == 2) {
        monitor_title=game->id;
        /* A catalog rejection is still a launch attempt. Give it the same
         * independent diagnostic file as every normal folder/NUP run. */
        FILE *rejected=fopen("sdmc:/BackupLoaderMKW-nup.log","w");
        FILE *crash_rejected=fopen("sdmc:/BackupLoaderMKW-crash.log","w");
        if(rejected){
          row(rejected,"BackupLoaderMKW experimental 0.15\n"
             "REPORT_FORMAT version=3 source=nup crash_path=sdmc:/BackupLoaderMKW-crash.log\n"
             "NUP_CATALOG_REFUSED name=%s path=%s title=%016llx reason=%s\n",
             game->name,game->root,
             (unsigned long long)game->id,game->nup_status);
          fclose(rejected);
        }
        if(crash_rejected){
          row(crash_rejected,"NX2_CRASH_DIAGNOSTIC version=2 game=%s guest=%016llx host=0400c3f00006e000\n"
            "REPORT_FORMAT version=3 source=nup primary_path=sdmc:/BackupLoaderMKW-nup.log\n"
            "PRELAUNCH_STOP phase=CATALOG_VALIDATION reason=%s\n",
            game->name,(unsigned long long)game->id,game->nup_status);
          fclose(crash_rejected);
        }
        snprintf(outcome,sizeof(outcome),"NUP not ready: %.95s",game->nup_status);
      } else {
        /* A = native with PresentTexture tracking; L+A = full native;
         * ZR+A = instrumented. ZR takes priority over L. */
        int mode = (held & HidNpadButton_ZR) ? 0 :
                   (held & HidNpadButton_L) ? 2 : 1;
        run(game, (held & HidNpadButton_B) != 0, mode);
        monitor_emit(0,0,0,0,1);
      }
    }
    draw(&catalog, selected);
    svcSleepThread(16666667);
  }
  monitor_emit(3,0,0,0,1);
  nx2_catalog_free(&catalog);
  consoleExit(NULL);
  return 0;
}
