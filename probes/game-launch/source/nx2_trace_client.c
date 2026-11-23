/* SPDX-License-Identifier: GPL-2.0-only
 * Private build: one-way, best-effort telemetry on its own thread.
 * No diagnostic IPC is ever performed on a game/launcher execution thread.
 * If IPC wedges, only this detached helper is affected. Drop events when full.
 */
#include "nx2_trace_client.h"
#ifdef NX2_DEV_TRACE
#include <switch.h>
#include <kaihen.h>
#include <string.h>
#include <stdint.h>
#define QCAP 64
#define TRACE_MAGIC 0x5432584eU
typedef struct {
  u32 magic, event;
  u64 guest_id, value;
} TraceRecord;
static struct {
  TraceRecord entries[QCAP];
  u32 read_pos, write_pos, dropped;
  u64 guest_id;
  u8 thread_stack[0x8000] __attribute__((aligned(16)));
  Thread thread;
  u32 launched;
} trace_state;
static void trace_worker(void *unused) {
  (void)unused;
  Service ro = {0}, hen = {0}, plugin = {0};
  Result rc = kaihenGetRoSession(&ro);
  if (R_SUCCEEDED(rc)) rc = kaihenOpenHomebrewSession(&ro,&hen);
  if (R_SUCCEEDED(rc)) {
    HenPluginName name = {0};
    memcpy(name.name,"nx2trace",sizeof("nx2trace"));
    rc = henOpenPlugin(&hen,&name,&plugin);
  }
  if (serviceIsActive(&hen)) serviceClose(&hen);
  if (R_FAILED(rc)) return; /* Plugin absent: all launches work normally. */
  for (;;) {
    u32 rd=__atomic_load_n(&trace_state.read_pos,__ATOMIC_ACQUIRE);
    u32 wr=__atomic_load_n(&trace_state.write_pos,__ATOMIC_ACQUIRE);
    if(rd==wr) {
      svcSleepThread(20000000);
      continue;
    }
    TraceRecord rec=trace_state.entries[rd%QCAP];
    /* Only this worker can block on plugin IPC, never a guest/game thread. */
    Result send=serviceDispatchIn(&plugin,0,rec);
    __atomic_store_n(&trace_state.read_pos,rd+1,__ATOMIC_RELEASE);
    if(R_FAILED(send)) break;
  }
  serviceClose(&plugin);
}
void nx2_trace_begin(uint64_t guest_id) {
  if(__atomic_load_n(&trace_state.launched,__ATOMIC_ACQUIRE)) {
    trace_state.guest_id=guest_id;
    return;
  }
  trace_state.guest_id=guest_id;
  Result rc=threadCreate(&trace_state.thread,trace_worker,NULL,
                         trace_state.thread_stack,sizeof(trace_state.thread_stack),
                         0x2c,-2);
  if(R_FAILED(rc)) return;
  if(R_FAILED(threadStart(&trace_state.thread))) {
    threadClose(&trace_state.thread);
    return;
  }
  __atomic_store_n(&trace_state.launched,1,__ATOMIC_RELEASE);
}
void nx2_trace_event(unsigned event,uint64_t value) {
  if(!__atomic_load_n(&trace_state.launched,__ATOMIC_ACQUIRE))return;
  u32 wr=__atomic_load_n(&trace_state.write_pos,__ATOMIC_RELAXED);
  u32 rd=__atomic_load_n(&trace_state.read_pos,__ATOMIC_ACQUIRE);
  if((u32)(wr-rd)>=QCAP){
    __atomic_add_fetch(&trace_state.dropped,1,__ATOMIC_RELAXED);
    return;
  }
  TraceRecord *rec=&trace_state.entries[wr%QCAP];
  *rec=(TraceRecord){TRACE_MAGIC,event,trace_state.guest_id,value};
  __atomic_store_n(&trace_state.write_pos,wr+1,__ATOMIC_RELEASE);
}
#endif
