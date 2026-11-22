/* SPDX-License-Identifier: GPL-2.0-only */
#include "launch.h"
#include "ipc_policy.h"
#include "guest_fsp_vfs.h"
#include "raw_svc.h"
#include "vm_memory.h"
#include <switch.h>
uint32_t nx2_virtual_info(uint64_t *out,uint32_t type,uint32_t handle,uint64_t sub){
 Nx2Launch *l=nx2_launch;
 /* The guest is executing inside MKW's process, but its own program ID
  * must remain the selected title's ID. Only intercept the current-process
  * identity query; retain real kernel results for all memory accounting. */
 if(out && l && handle==CUR_PROCESS_HANDLE && sub==0 &&
    type==InfoType_ProgramId && l->guest_program_id){
   *out=l->guest_program_id;
   nx2_rpc(RPC_LOG,l->guest_program_id,0,0,0,0,
           "GUEST_PROGRAM_ID",0,0);
   return 0;
 }
 if(__atomic_load_n(&nx2_native_vm_active,__ATOMIC_ACQUIRE)){
   /* The guest's heap address must agree with SetHeapSize's isolated
    * return value. Leave the kernel's own heap base unchanged for host
    * code, which calls svcGetInfo directly rather than this guest hook. */
   if(l && out && handle==CUR_PROCESS_HANDLE && !sub &&
      type==4 && l->heap_base){
     *out=l->heap_base;
     return 0;
   }
   /* Preserve the real kernel's memory accounting. A bounded trace of
    * guest-visible queries also captures values after SetHeapSize, unlike
    * the old first-per-type log. The host prints all four RPC fields. */
   uint32_t rc=nx2_raw_info(out,type,handle,sub);
   if(l && out && handle==CUR_PROCESS_HANDLE && !sub &&
      (type==5 || type==6 || type==7 || type==16 ||
       type==21 || type==22)){
     uint64_t kernel_value=(!rc)?*out:0;
     /* Only the retained host heap prefix is excluded from the
      * guest's non-system accounting. Guest NSO backing is mapped in
      * separate RO memory and is NOT part of this prefix; it remains
      * counted in UsedNonSystemMemorySize. Subtract from both total and
      * used (types 21 and 22) so the apparent free budget is unchanged.
      * Native heap and physical allocation checks continue to use
      * unmodified kernel values. Never underflow the reported usage. */
     if(!rc && (type==21 || type==22) &&
        nx2_process_heap_status==NX2_HEAP_READY &&
        nx2_process_heap.host_bytes &&
        kernel_value>=nx2_process_heap.host_bytes){
       *out=kernel_value-nx2_process_heap.host_bytes;
       nx2_rpc(RPC_LOG,kernel_value,*out,nx2_process_heap.host_bytes,type,0,
               "GUEST_MEMORY_ACCOUNTING_EXCLUDE_HOST",0,0);
     }
     static uint32_t budget_query_count=0;
     uint32_t ordinal=__atomic_fetch_add(&budget_query_count,1,__ATOMIC_RELAXED);
     if(ordinal<256)
       nx2_rpc(RPC_LOG,rc?0:*out,type,rc,
               nx2_process_heap.host_bytes,0,
               "GUEST_MEMORY_INFO_QUERY",0,0);
   }
   return rc;
 }
 if(handle==CUR_PROCESS_HANDLE&&!sub){
  switch(type){
   case 4:*out=l->heap_base;return 0;
   case 5:case 6:case 21:*out=l->heap_capacity;return 0;
   case 7:case 22:*out=__atomic_load_n(&l->heap_size,__ATOMIC_ACQUIRE);return 0;
  }
 }
 return nx2_raw_info(out,type,handle,sub);
}
/* Native SDK heap requests must go through the actual kernel, not a
 * fake arena pointer. Preserve the host allocator's committed prefix,
 * stay in the game's original reserved heap span, and keep physical
 * headroom for native physical-memory maps. */
/* EXPERIMENTAL: 2.5 GiB minimum to test whether the kernel can
 * satisfy the guest after accounting/isolation. Do not merge into the
 * stable branch without real physical-map telemetry and ownership review. */
#define NX2_NATIVE_PHYSICAL_FLOOR UINT64_C(0xA0000000) /* 2.5 GiB */
uint32_t nx2_virtual_heap(void **out,uint64_t size){
 Nx2Launch *l=nx2_launch;
 if(!out || !size || (size & (NX2_GAME_ALIGNMENT-1)))return 0xd001;
 if(!__atomic_load_n(&nx2_native_vm_active,__ATOMIC_ACQUIRE)){
   if(size>l->heap_capacity)return 0xd001;
   __atomic_store_n(&l->heap_size,size,__ATOMIC_RELEASE);
   *out=(void *)(uintptr_t)l->heap_base;
   nx2_rpc(RPC_LOG,size,0,0,0,0,"SetHeapSize handled in private arena",0,0);
   return 0;
 }
 const Nx2HeapPlan *plan=&nx2_process_heap;
 uint64_t base=0,total=0,used=0;
 Result rc=svcGetInfo(&base,4,CUR_PROCESS_HANDLE,0);
 if(R_FAILED(rc))return rc;
 rc=svcGetInfo(&total,6,CUR_PROCESS_HANDLE,0);
 if(R_FAILED(rc))return rc;
 rc=svcGetInfo(&used,7,CUR_PROCESS_HANDLE,0);
 if(R_FAILED(rc))return rc;
 /* Query the end of the retained heap instead of assuming a QueryMemory
  * result at the heap base covers the entire committed allocation. It need
  * not: the host's mapped heap can be split into multiple VM regions. */
 const uint64_t original_end=plan->original_begin+plan->original_bytes;
 const uint64_t cut=plan->game_begin;
 uint64_t retain=cut-base;
 uint64_t previous_guest=__atomic_load_n(&l->heap_size,__ATOMIC_ACQUIRE);
 /* For repeated resizes, the memory at cut is already guest heap.
  * Verify the end of the *current* allocation instead of requiring
  * cut to remain unmapped forever. */
 if(plan->original_bytes>UINT64_MAX-plan->original_begin ||
    plan->original_begin<base || cut<base || cut>original_end ||
    original_end<base || previous_guest>original_end-cut ||
    size>original_end-cut || size>UINT64_MAX-retain ||
    (previous_guest && l->heap_base!=cut)) {
   nx2_rpc(RPC_LOG,size,retain,previous_guest,original_end,0,
           "GUEST_HEAP_RANGE_REFUSED",0,0);
   return 0xd001;
 }
 const uint64_t boundary=cut+previous_guest;
 MemoryInfo end_region,tail;u32 page=0;
 rc=svcQueryMemory(&end_region,&page,boundary-1);
 if(R_FAILED(rc))return rc;
 rc=svcQueryMemory(&tail,&page,boundary);
 if(R_FAILED(rc))return rc;
 uint64_t available=total>=used?total-used:0;
 uint64_t end_of_commit=end_region.addr+end_region.size;
 nx2_rpc(RPC_LOG,size,retain,end_of_commit,available,0,
         "NATIVE_HEAP_BOUNDS_INPUT",0,0);
 nx2_rpc(RPC_LOG,boundary,tail.addr,tail.type,tail.perm,0,
         "NATIVE_HEAP_TAIL_INPUT",0,0);
 if(end_region.addr>boundary-1 ||
    end_region.size>UINT64_MAX-end_region.addr ||
    end_of_commit!=boundary || end_region.type!=MemType_Heap ||
    (end_region.perm&Perm_Rw)!=Perm_Rw ||
    tail.type!=MemType_Unmapped || tail.perm!=Perm_None) {
   nx2_rpc(RPC_LOG,size,previous_guest,end_of_commit,tail.type,0,
           "NATIVE_HEAP_BOUNDS_REFUSED",0,0);
   return 0xd001;
 }
 const uint64_t kernel_size=retain+size;
 const uint64_t increase=size>previous_guest?size-previous_guest:0;
 nx2_rpc(RPC_LOG,previous_guest,size,kernel_size,NX2_NATIVE_PHYSICAL_FLOOR,0,
         "GUEST_HEAP_RESIZE_GUARD",0,0);
 if(!nx2_heap_growth_allowed(previous_guest,size,available,
                             NX2_NATIVE_PHYSICAL_FLOOR)) {
   nx2_rpc(RPC_LOG,size,kernel_size,available,NX2_NATIVE_PHYSICAL_FLOOR,0,
           "GUEST_HEAP_BUDGET_REFUSED",0,0);
   return 0xd001;
 }
 nx2_rpc(RPC_LOG,size,kernel_size,cut,available,0,
         "GUEST_HEAP_ISOLATED_REQUEST",0,0);
 void *returned=NULL;
 rc=svcSetHeapSize(&returned,kernel_size);
 nx2_rpc(RPC_LOG,size,kernel_size,available,rc,0,
         "GUEST_HEAP_SET_RESULT",0,0);
 if(R_FAILED(rc))return rc;
 if((uintptr_t)returned!=base) {
   nx2_rpc(RPC_ABORT,(uintptr_t)returned,base,size,0,0,
           "Native heap base changed unexpectedly",0,0);
   for(;;)__asm__ volatile("wfe");
 }
 /* kernel_size includes the retained prefix; the guest's heap starts
  * exactly at the first free page after the launcher partition. */
 *out=(void *)(uintptr_t)cut;
 l->heap_base=cut;
 __atomic_store_n(&l->heap_size,size,__ATOMIC_RELEASE);
 nx2_rpc(RPC_LOG,cut,size,kernel_size,0,0,
         "GUEST_HEAP_ISOLATED_READY",0,0);
 return 0;
}
uint32_t nx2_rtld_query(void *memory,uint32_t *page,uint64_t address){Nx2Launch *l=nx2_launch;uint64_t begin=0,end=UINT64_C(0x8000000000);uint32_t type=0,perm=0;for(uint32_t i=0;i<l->region_count;i++){Nx2Region *r=&l->regions[i];if(address>=r->begin&&address<r->end){begin=r->begin;end=r->end;type=r->type;perm=r->permission;break;}if(r->end<=address&&r->end>begin)begin=r->end;if(r->begin>address&&r->begin<end)end=r->begin;}if(!memory)return 0xdc01;if(address>=UINT64_C(0x8000000000)){begin=UINT64_C(0x8000000000);end=0;type=0x10;perm=0;}uint64_t *q=memory;uint32_t *w=memory;q[0]=begin;q[1]=end-begin;w[4]=type;w[5]=0;w[6]=perm;w[7]=w[8]=w[9]=0;if(page)*page=0;return 0;}
static uint32_t tag(uint32_t h){for(unsigned i=0;i<128;i++){uint64_t v=__atomic_load_n(&nx2_launch->handle_tags[i],__ATOMIC_ACQUIRE);if((uint32_t)v==h&&h)return v>>32;}return 0;}
static void set_tag(uint32_t h,uint32_t kind){if(!h)return;for(unsigned i=0;i<128;i++){uint64_t v=__atomic_load_n(&nx2_launch->handle_tags[i],__ATOMIC_RELAXED);if((uint32_t)v==h){__atomic_store_n(&nx2_launch->handle_tags[i],kind?((uint64_t)kind<<32)|h:0,__ATOMIC_RELEASE);return;}}if(!kind)return;for(unsigned i=0;i<128;i++){uint64_t expected=0;if(__atomic_compare_exchange_n(&nx2_launch->handle_tags[i],&expected,((uint64_t)kind<<32)|h,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED))return;}nx2_rpc(RPC_ABORT,h,0,0,0,0,"Service handle tag table full",0,0);for(;;)__asm__ volatile("wfe");}
uint32_t nx2_connect(uint32_t *out,const char *name){nx2_rpc(RPC_LOG,0x1f,0,0,0,0,"CONNECT_TO_NAMED_PORT_BEGIN",name,0);uint32_t rc=nx2_raw_connect(out,name);if(!rc&&name&&name[0]=='s'&&name[1]=='m'&&name[2]==':'&&!name[3])set_tag(*out,1);nx2_rpc(RPC_LOG,rc,rc?0:*out,0x1f,0,0,"CONNECT_TO_NAMED_PORT_RESULT",name,0);return rc;}
static uint32_t word(const unsigned char *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
/* Best-effort diagnostic ownership, deliberately separate from the SM/FSP
 * enforcement tags. A slot never authorizes an IPC request. */
typedef struct {uint32_t handle;char service[9];uint8_t root_first_pending;} Nx2IpcTraceHandle;
static Nx2IpcTraceHandle ipc_owners[256];
static volatile uint32_t ipc_trace_lock;
static void trace_lock(void){
 while(__atomic_exchange_n(&ipc_trace_lock,1,__ATOMIC_ACQUIRE))
   __asm__ volatile("yield");
}
static void trace_unlock(void){__atomic_store_n(&ipc_trace_lock,0,__ATOMIC_RELEASE);}
/* A single FSP session handle can contain multiple CMIF domain objects.
 * Only grant a read-only reader's command to its specific object ID. */
typedef struct {uint32_t handle,object,kind;} Nx2DomainCapability;
static Nx2DomainCapability domain_caps[128];
static void domain_cap_set(uint32_t h,uint32_t object,uint32_t kind){
 if(!h||!object||!kind)return;
 trace_lock();
 unsigned slot=128;
 for(unsigned i=0;i<128;i++){
   if(domain_caps[i].handle==h && domain_caps[i].object==object){
     slot=i;break;
   }
   if(slot==128 && !domain_caps[i].handle)slot=i;
 }
 if(slot<128)
   domain_caps[slot]=(Nx2DomainCapability){h,object,kind};
 trace_unlock();
}
static uint32_t domain_cap_kind(uint32_t h,uint32_t object){
 uint32_t kind=0;
 trace_lock();
 for(unsigned i=0;i<128;i++){
   if(domain_caps[i].handle==h && domain_caps[i].object==object){
     kind=domain_caps[i].kind;break;
   }
 }
 trace_unlock();
 return kind;
}
/* HIPC CloneCurrentObject / CloneCurrentObjectEx preserves domain
 * objects on a cloned FSP session. Retain verified reader permissions
 * without widening the new handle to a general FSP capability. */
static unsigned domain_cap_clone_handle(uint32_t source,uint32_t clone){
 if(!source||!clone||source==clone)return 0;
 unsigned copied=0;
 trace_lock();
 for(unsigned i=0;i<128;i++){
  if(domain_caps[i].handle!=source||domain_caps[i].kind!=3)continue;
  uint32_t object=domain_caps[i].object;
  unsigned free_slot=128,already=0;
  for(unsigned j=0;j<128;j++){
   if(domain_caps[j].handle==clone &&
      domain_caps[j].object==object){already=1;break;}
   if(free_slot==128&&!domain_caps[j].handle)free_slot=j;
  }
  if(!already&&free_slot<128){
   domain_caps[free_slot]=(Nx2DomainCapability){clone,object,3};
   copied++;
  }
 }
 trace_unlock();
 return copied;
}
static void domain_cap_forget_handle(uint32_t h){
 trace_lock();
 for(unsigned i=0;i<128;i++)if(domain_caps[i].handle==h)
   domain_caps[i]=(Nx2DomainCapability){0};
 trace_unlock();
}
static void domain_cap_forget_object(uint32_t h,uint32_t object){
 trace_lock();
 for(unsigned i=0;i<128;i++)
   if(domain_caps[i].handle==h && domain_caps[i].object==object)
     domain_caps[i]=(Nx2DomainCapability){0};
 trace_unlock();
}
static void trace_owner(uint32_t h,const char *name,int is_service_root){
 if(!h||!name||!name[0])return;
 trace_lock();
 unsigned slot=256;
 for(unsigned i=0;i<256;i++){
   if(ipc_owners[i].handle==h){slot=i;break;}
   if(slot==256&&!ipc_owners[i].handle)slot=i;
 }
 if(slot<256){
   if(ipc_owners[slot].handle!=h)ipc_owners[slot].root_first_pending=0;
   ipc_owners[slot].handle=h;
   unsigned i=0;for(;i<8&&name[i];i++)ipc_owners[slot].service[i]=name[i];
   ipc_owners[slot].service[i]=0;
   if(is_service_root)ipc_owners[slot].root_first_pending=1;
 }
 trace_unlock();
}
static int trace_claim_first_root(uint32_t h){
 int found=0;
 trace_lock();
 for(unsigned i=0;i<256;i++){
   if(ipc_owners[i].handle==h && ipc_owners[i].root_first_pending){
     ipc_owners[i].root_first_pending=0;
     found=1;break;
   }
 }
 trace_unlock();
 return found;
}
static void trace_name(uint32_t h,char out[9]){
 out[0]=0;
 trace_lock();
 for(unsigned i=0;i<256;i++)if(ipc_owners[i].handle==h){
   for(unsigned j=0;j<9;j++)out[j]=ipc_owners[i].service[j];
   break;
 }
 trace_unlock();
}
static void trace_forget(uint32_t h){
 trace_lock();
 for(unsigned i=0;i<256;i++)if(ipc_owners[i].handle==h){
   ipc_owners[i].handle=0;ipc_owners[i].service[0]=0;
   ipc_owners[i].root_first_pending=0;break;
 }
 trace_unlock();
}
/* The HIPC special header contains copy and move handles. Read no more
 * than the provided request/reply buffer, and emit only handle IDs. */
static void trace_reply_handles(const unsigned char *p,uint64_t n,
                                uint32_t parent,const char *owner,int root){
 if(!p||n<12||!(word(p+4)&0x80000000U))return;
 uint32_t sh=word(p+8),copies=(sh>>1)&15U,moves=(sh>>5)&15U;
 uint64_t off=12+((sh&1U)?8U:0U);
 if(off>n || (uint64_t)(copies+moves)*4U>n-off)return;
 for(uint32_t i=0;i<copies+moves;i++){
   uint32_t child=word(p+off+4U*i);
   if(!child)continue;
   trace_owner(child,owner,root);
   /* Handle ownership remains tracked after save mount, but successful
    * child acquisition needn't block its caller on a diagnostic RPC. */
   if(!__atomic_load_n(&nx2_launch->late_trace_active,__ATOMIC_ACQUIRE))
     nx2_rpc(RPC_LOG,child,parent,i>=copies,sh,0,
             "IPC_RETURNED_HANDLE",owner,0);
 }
}
/* Only MOVED handles returned by an IPC method become child sessions.
 * A COPY handle is not automatically a newly acquired service interface
 * and must never inherit FSP/SM permissions on the basis of its neighbor. */
static void response_tags(const unsigned char *p,uint64_t n,uint32_t kind){
 if(!p||n<12||!(word(p+4)&0x80000000U))return;
 uint32_t sh=word(p+8),copy=(sh>>1)&15U,move=(sh>>5)&15U;
 uint64_t off=12U+((sh&1U)?8U:0U);
 if(off>n||(uint64_t)(copy+move)*4U>n-off)return;
 off+=(uint64_t)copy*4U;
 for(uint32_t i=0;i<move;i++)set_tag(word(p+off+4U*i),kind);
}
/* Every guest SVC gets request and completion records, including untagged
 * services and virtual filesystem replies. Payloads and UID data are excluded.
 * Publication never blocks on SD logging or an RPC from the guest thread. */
static void ipc_event_emit(uint32_t call_id,uint32_t phase,uint32_t h,
                           uint32_t kind,Nx2IpcDecision d,
                           uint32_t request_type,uint32_t flags,
                           uint32_t hdr0,uint32_t hdr1,uint32_t special,
                           uint32_t kernel,uint32_t cmif,
                           uintptr_t caller,const char *name){
 Nx2Launch *l=nx2_launch;
 uint32_t seq=__atomic_fetch_add(&l->ipc_event_next,1,__ATOMIC_RELAXED);
 Nx2IpcEvent *e=&l->ipc_events[seq%NX2_IPC_EVENT_SLOTS];
 __atomic_store_n(&e->sequence,0,__ATOMIC_RELAXED);
 e->caller=caller;e->tick=armGetSystemTick();e->call_id=call_id;
 e->phase=phase;e->handle=h;e->kind=kind;e->request_type=request_type;
 e->command=d.valid?d.command:UINT32_MAX;
 e->object_id=d.domain?d.object_id:0;
 e->flags=flags;e->hdr0=hdr0;e->hdr1=hdr1;e->special=special;
 e->kernel_result=kernel;e->cmif_result=cmif;
 unsigned j=0;
 if(name)for(;j<8&&name[j];j++)e->service[j]=name[j];
 for(;j<9;j++)e->service[j]=0;
 __atomic_store_n(&e->sequence,seq+1,__ATOMIC_RELEASE);
}
static uint32_t filter_sync(void *buffer,uint64_t n,uint32_t h,int external,uintptr_t caller){
 unsigned char *p=buffer;uint32_t kind=tag(h);
 Nx2IpcDecision d=nx2_ipc_decide(p,n,kind);
 uint32_t cmd=d.command,has=d.valid,control=d.control,want_fsp=d.want_fsp;
 uint64_t scan=n<256?n:256;
 uint32_t request_type=n>=4?word(p)&65535:0;
 char service[9]={0},owner[9]={0};
 if(kind==1)nx2_ipc_service_name(p,n,service);
 trace_name(h,owner);
 const uint32_t hdr0=n>=4?word(p):0;
 const uint32_t hdr1=n>=8?word(p+4):0;
 const uint32_t special=n>=12&&(hdr1&0x80000000U)?word(p+8):0;
 const char *trace_label=(kind==1&&service[0])?service:owner;
 const uint32_t flags=(d.valid?1U:0U)|(d.domain?2U:0U)|
                      (d.control?4U:0U)|(external?8U:0U)|
                      (d.close_kind?16U:0U)|(want_fsp?32U:0U);
 uint32_t call_id=__atomic_add_fetch(&nx2_launch->ipc_call_next,1,__ATOMIC_RELAXED);
 ipc_event_emit(call_id,1,h,kind,d,request_type,flags,hdr0,hdr1,
                special,UINT32_MAX,UINT32_MAX,caller,trace_label);
 /* A first request to a newly acquired SM service is a valuable
  * last-operation marker if the process exits before any snapshot. */
 int first_root_method=(kind==0 && request_type!=2 && owner[0] &&
                        trace_claim_first_root(h));
 if(first_root_method)
   nx2_rpc(RPC_LOG,h,request_type,cmd,caller,0,
           "SERVICE_ROOT_FIRST_IPC_BEGIN",owner,0);
 /* Snapshot before SVC: kernel may replace TLS request bytes with its
  * reply. No payload contents, guest pointers, or sensitive IDs logged. */
 /* Classify native filesystem capabilities by interface and command.
  * This never grants new kernel permissions or mutates host save data.
  * A cache save-info reader is an explicitly read-only child object. */
 uint32_t domain_kind=d.domain?domain_cap_kind(h,d.object_id):0;
 /* Apply service permissions to the target domain object, not just the
  * underlying session handle. Do not grant command 0 globally to FSP. */
 int reader_domain=(kind==2 && d.domain && domain_kind==3);
 int closing=d.valid && d.close_kind!=0;
 /* Route native FS-domain interfaces to this title's private SD backend.
  * No guest save objects are ever sent to the host title's fsp-srv. */
 if(kind==2 && !reader_domain &&
    nx2_guest_fsp_route(p,n,h,kind,d)){
   uint32_t guest_rc=UINT32_MAX;
   (void)nx2_ipc_reply_result(p,n,request_type,&guest_rc);
   ipc_event_emit(call_id,3,h,kind,d,request_type,flags,hdr0,hdr1,
                  special,0,guest_rc,caller,trace_label);
   return 0;
 }
 if(reader_domain && !closing && nx2_ipc_save_reader_allowed(d))
   nx2_rpc(RPC_LOG,h,d.object_id,cmd,d.domain,0,
           "FSP_DOMAIN_READ_ONLY_REQUEST_ALLOWED",owner,0);
 if((kind==2||kind==3) && closing)
   nx2_rpc(RPC_LOG,h,d.object_id,d.close_kind,request_type,0,
           "FSP_IPC_CLOSE_FORWARDED",owner,0);
 if(kind==2 && !closing &&
    !(reader_domain?nx2_ipc_save_reader_allowed(d):
                    nx2_ipc_fsp_allowed(d))){
   nx2_rpc(RPC_LOG,((uint64_t)hdr0<<32)|hdr1,
           ((uint64_t)special<<32)|request_type,
           ((uint64_t)d.domain<<32)|d.object_id,caller,0,
           "FSP_BLOCKED_REQUEST_HEADER",owner,0);
   nx2_rpc(RPC_LOG,h,d.object_id,d.domain,cmd,0,
           "FSP_DOMAIN_REQUEST_BLOCKED",owner,0);
   __atomic_fetch_add(&nx2_launch->fsp_denied,1,__ATOMIC_RELAXED);
   /* The guest can handle an unsupported IPC operation itself. Never
    * acknowledge success for missing functionality or forward it into
    * the host application's save permissions. */
   if(nx2_guest_fsp_unsupported(p,n,d)){
     nx2_rpc(RPC_LOG,h,d.object_id,cmd,NX2_FS_UNSUPPORTED,0,
             "GUEST_FSP_UNSUPPORTED_RESPONSE",owner,0);
     ipc_event_emit(call_id,4,h,kind,d,request_type,flags,hdr0,hdr1,
                    special,0,NX2_FS_UNSUPPORTED,caller,trace_label);
     return 0;
   }
   /* An unsupported HIPC control or malformed CMIF cannot safely
    * receive a fabricated reply. Return a genuine kernel-level error
    * to the guest instead of permanently parking its thread here.
    * The game/SDK may handle that error; no host FSP access is granted. */
   uint32_t fail=has?NX2_FS_UNSUPPORTED:NX2_INVALID;
   ipc_event_emit(call_id,5,h,kind,d,request_type,flags,hdr0,hdr1,
                  special,fail,UINT32_MAX,caller,trace_label);
   return fail;
 }
 if(kind==3&&!closing&&!nx2_ipc_save_reader_allowed(d)){
   if(nx2_guest_fsp_unsupported(p,n,d)){
     ipc_event_emit(call_id,4,h,kind,d,request_type,flags,hdr0,hdr1,
                    special,0,NX2_FS_UNSUPPORTED,caller,trace_label);
     return 0;
   }
   /* Keep the read-only iterator isolated from host saves. A bad
    * command is a recoverable guest-visible error, not a loader hang. */
   uint32_t fail=has?NX2_FS_UNSUPPORTED:NX2_INVALID;
   ipc_event_emit(call_id,5,h,kind,d,request_type,flags,hdr0,hdr1,
                  special,fail,UINT32_MAX,caller,trace_label);
   return fail;
 }
 /* HIPC Control 1 exports a domain object as a standalone handle.
  * Preserve its verified reader kind before native IPC overwrites TLS. */
 uint32_t copied_reader=0;
 if(kind==2&&control&&cmd==1){
  uint32_t copied_object=0;
  if(nx2_ipc_control_copy_domain_object(p,n,&copied_object))
   copied_reader=domain_cap_kind(h,copied_object)==3;
 }
 /* Avoid two synchronous host RPCs around every game IPC SVC.
  * Record the pre/post state in shared memory instead, including the
  * calls made after save mounting. The host snapshots this ring. */
 Nx2IpcBreadcrumb *crumb=0,*graphics_crumb=0;
 const char *graphics_owner=(kind==1&&service[0])?service:owner;
 const int graphics_service=
   ((graphics_owner[0]=='v'&&graphics_owner[1]=='i'&&
      (graphics_owner[2]==':'||graphics_owner[2]==0))||
    (graphics_owner[0]=='n'&&graphics_owner[1]=='v'&&
     graphics_owner[2]=='d'&&graphics_owner[3]=='r'&&
     graphics_owner[4]=='v'));
 if(request_type!=2){
   uint32_t seq=__atomic_fetch_add(&nx2_launch->ipc_trace_next,1,
                                  __ATOMIC_RELAXED);
   crumb=&nx2_launch->ipc_breadcrumbs[seq%NX2_IPC_TRACE_SLOTS];
   __atomic_store_n(&crumb->phase,0,__ATOMIC_RELAXED);
   crumb->handle=h;crumb->caller=caller;crumb->request_type=request_type;
   crumb->command=has?cmd:UINT32_MAX;crumb->result=UINT32_MAX;
   crumb->service_result=UINT32_MAX;
   const char *label=(kind==1 && service[0])?service:owner;
   for(unsigned j=0;j<9;j++){
     crumb->service[j]=label[j];
     if(!label[j])break;
   }
   crumb->service[8]=0;
   __atomic_store_n(&crumb->phase,1,__ATOMIC_RELEASE);
   __atomic_fetch_add(&nx2_launch->ipc_trace_begins,1,__ATOMIC_RELAXED);
   if(graphics_service){
     uint32_t gs=__atomic_fetch_add(&nx2_launch->graphics_ipc_next,1,__ATOMIC_RELAXED);
     graphics_crumb=&nx2_launch->graphics_ipc[gs%NX2_GRAPHICS_TRACE_SLOTS];
     __atomic_store_n(&graphics_crumb->phase,0,__ATOMIC_RELAXED);
     graphics_crumb->handle=h;graphics_crumb->caller=caller;
     graphics_crumb->request_type=request_type;
     graphics_crumb->command=has?cmd:UINT32_MAX;
     graphics_crumb->result=UINT32_MAX;graphics_crumb->service_result=UINT32_MAX;
     for(unsigned j=0;j<9;j++){
       graphics_crumb->service[j]=graphics_owner[j];
       if(!graphics_owner[j])break;
     }
     graphics_crumb->service[8]=0;
     __atomic_store_n(&graphics_crumb->phase,1,__ATOMIC_RELEASE);
   }
 }
 /* The ring entry above is published BEFORE the native SVC. Previously
  * every tagged guest IPC synchronously waited for a storage-worker RPC
  * solely to print IPC_BEGIN. That extra guest/SD dependency is unnecessary
  * and can postpone service initialization or graphics work. */
 uint32_t rc=external?nx2_raw_buffer(buffer,n,h):nx2_raw_sync(h);
 if(crumb){
   crumb->result=rc;
   __atomic_store_n(&crumb->phase,2,__ATOMIC_RELEASE);
   __atomic_fetch_add(&nx2_launch->ipc_trace_ends,1,__ATOMIC_RELAXED);
 }
 if(graphics_crumb){
   graphics_crumb->result=rc;
   __atomic_store_n(&graphics_crumb->phase,2,__ATOMIC_RELEASE);
 }
 /* Close operations are management messages, not filesystem methods.
  * Release the associated local capability after the kernel processes them. */
 if(kind && d.close_kind==1 && !rc)
   domain_cap_forget_object(h,d.object_id);
 /* HIPC type 2 destroys the remote session. A successful close or
  * ConnectionClosed result invalidates its service identity even if the
  * guest does not follow up with svcCloseHandle. Retaining stale tags
  * allows a recycled handle to inherit an unrelated service's FSP rights.
  * SSL close (0xF601) in the Minecraft hardware trace exercises this. */
 if(nx2_ipc_session_closed(d,rc)){
   set_tag(h,0);
   trace_forget(h);
   domain_cap_forget_handle(h);
 }
 uint32_t service_result=0;
 int reply_decoded=!rc&&nx2_ipc_reply_result(p,n,request_type,&service_result);
 ipc_event_emit(call_id,2,h,kind,d,request_type,flags,hdr0,hdr1,
                special,rc,reply_decoded?service_result:UINT32_MAX,
                caller,trace_label);
 if(first_root_method)
   nx2_rpc(RPC_LOG,h,rc,reply_decoded?service_result:UINT32_MAX,cmd,0,
           "SERVICE_ROOT_FIRST_IPC_RESULT",owner,0);
 if(crumb && reply_decoded)
   __atomic_store_n(&crumb->service_result,service_result,__ATOMIC_RELEASE);
 if(graphics_crumb && reply_decoded)
   __atomic_store_n(&graphics_crumb->service_result,service_result,__ATOMIC_RELEASE);
 /* The completed IPC_EVENT and breadcrumb already contain CMIF status.
  * Do not block guest service calls on repetitive success-only SD logs.
  * Error replies remain persisted by the main log's abnormal-event path. */
 if(!rc){
   /* A kernel-successful SendSyncRequest can still return a CMIF
    * service error (e.g. 0x1015 on an unavailable service alias).
    * Never classify a returned handle from that failed service request. */
   if(want_fsp && reply_decoded && !service_result)
     response_tags(p,scan,2);
   else if(kind==2 && !control && cmd==62 &&
           reply_decoded && !service_result) {
     /* On a domain session the output object ID is NOT a new kernel
      * handle. The previous allowlist missed this and rejected a read
      * through the same FSP handle at command 0. */
     uint32_t child=0;
     if(d.domain &&
        nx2_ipc_domain_response_first_object(p,n,&child)){
       domain_cap_set(h,child,3);
       nx2_rpc(RPC_LOG,h,child,cmd,d.object_id,0,
               "FSP_DOMAIN_READER_CAPABILITY_REGISTERED",owner,0);
     }else{
       /* Non-domain output objects are moved handles. */
       response_tags(p,scan,3);
       nx2_rpc(RPC_LOG,h,cmd,d.domain,d.object_id,0,
               "FSP_READER_NON_DOMAIN_OR_UNVERIFIED_RESPONSE",owner,0);
     }
   }
   else if(kind&&control&&(cmd==1||cmd==2||cmd==4)&&
           reply_decoded&&!service_result){
     /* CopyFromCurrentDomain produces ONE moved reader handle.
      * Never retag unrelated copied handles as reader sessions. */
     if(kind==2&&cmd==1&&copied_reader){
       /* A malformed/failed reader export must never be promoted to
        * a general-purpose fsp-srv session as a fallback. */
       if(reply_decoded&&!service_result){
         uint32_t reader=0;
         if(nx2_ipc_reply_first_moved_handle(p,scan,&reader))
           set_tag(reader,3);
       }
     }else response_tags(p,scan,kind);
     /* Session clones inherit only previously validated reader objects. */
     if(kind==2&&(cmd==2||cmd==4)&&reply_decoded&&!service_result){
       uint32_t clone=0;
       if(nx2_ipc_reply_first_moved_handle(p,n,&clone)){
         unsigned count=domain_cap_clone_handle(h,clone);
         if(count)
           nx2_rpc(RPC_LOG,h,clone,count,cmd,0,
                   "FSP_READER_DOMAIN_CAPABILITIES_CLONED",owner,0);
       }
     }
   }
   /* An SM GetService response identifies the new session's owner.
    * Also trace returned child handles from any already classified
    * session, without changing access-control tags. */
   if(reply_decoded && !service_result){
     if(kind==1&&service[0])trace_reply_handles(p,scan,h,service,1);
     else if(owner[0])trace_reply_handles(p,scan,h,owner,0);
   }
 }
 if(rc && request_type==2 && rc==0xF601){
   nx2_rpc(RPC_LOG,h,rc,caller,kind,0,
           "IPC_CLOSE_CONNECTION_CLOSED",owner,0);
 }
 if(rc && !(request_type==2 && rc==0xF601)){
   nx2_rpc(RPC_LOG,((uint64_t)hdr0<<32)|hdr1,
           ((uint64_t)special<<32)|request_type,
           ((uint64_t)cmd<<32)|(has?1U:0U)|(control?2U:0U),
           ((uint64_t)external<<32)|n,0,
           "IPC_FAILED_REQUEST_HEADER",owner,0);
   nx2_rpc(RPC_LOG,h,rc,kind,request_type,0,
           "IPC_FAILED_HANDLE_OWNER",owner,0);
 }
 /* The host RPC_IPC handler was logging-only. Preserve actual error
  * telemetry above and in IPC_EVENT without crossing the guest RPC gate. */
 return rc;
}
uint32_t nx2_sync(uint32_t h){
 void *tlr;__asm__ volatile("mrs %0,tpidrro_el0":"=r"(tlr));
 return filter_sync(tlr,256,h,0,(uintptr_t)__builtin_return_address(0));
}
uint32_t nx2_sync_buffer(void *p,uint64_t n,uint32_t h){
 return filter_sync(p,n,h,1,(uintptr_t)__builtin_return_address(0));
}
uint32_t nx2_close_handle(uint32_t h){
 uint32_t kind=tag(h);
 Nx2IpcDecision empty={0};
 char owner[9]={0};trace_name(h,owner);
 uint32_t call_id=__atomic_add_fetch(&nx2_launch->ipc_call_next,1,__ATOMIC_RELAXED);
 uintptr_t caller=(uintptr_t)__builtin_return_address(0);
 ipc_event_emit(call_id,1,h,kind,empty,2,0x80U,0,0,0,
                UINT32_MAX,UINT32_MAX,caller,owner);
 uint32_t rc=nx2_raw_close(h);
 ipc_event_emit(call_id,2,h,kind,empty,2,0x80U,0,0,0,
                rc,UINT32_MAX,caller,owner);
 if(!rc){set_tag(h,0);trace_forget(h);domain_cap_forget_handle(h);}
 return rc;
}
__attribute__((noinline,optimize("no-omit-frame-pointer")))
void nx2_break(uint32_t reason,uint64_t address,uint64_t size){
 uintptr_t caller=(uintptr_t)__builtin_return_address(0);
 uintptr_t fp=0;
 /* Capture the AArch64 frame pointer, not this wrapper's stack pointer:
  * the abort origin is above the SDK's generic Abort(Result*) routine.
  * The host validates every frame link before dereferencing it. */
 __asm__ volatile("mov %0, x29":"=r"(fp));
 nx2_rpc(RPC_BREAK,reason,address,size,caller,(void *)fp,
         "Native SDK Break",0,0);
 for(;;)__asm__ volatile("wfe");
}
void nx2_debug(const char *text,uint64_t n){char buf[NX2_PATH_MAX];if(n>=sizeof(buf))n=sizeof(buf)-1;for(uint64_t i=0;i<n;i++)buf[i]=text[i];buf[n]=0;nx2_rpc(RPC_LOG,0,0,0,0,0,buf,0,0);}

uint32_t nx2_async(uint32_t *out,void *p,uint64_t n,uint32_t h){
 uint32_t kind=tag(h);
 Nx2IpcDecision d=nx2_ipc_decide(p,n,kind);
 uint32_t hdr0=p&&n>=4?word(p):0;
 uint32_t hdr1=p&&n>=8?word((const unsigned char*)p+4):0;
 uint32_t special=p&&n>=12&&(hdr1&0x80000000U)?
                  word((const unsigned char*)p+8):0;
 uint32_t type=hdr0&0xffffU;
 char owner[9]={0};
 if(kind==1)nx2_ipc_service_name(p,n,owner);
 if(!owner[0])trace_name(h,owner);
 uint32_t call_id=__atomic_add_fetch(&nx2_launch->ipc_call_next,1,__ATOMIC_RELAXED);
 uintptr_t caller=(uintptr_t)__builtin_return_address(0);
 ipc_event_emit(call_id,1,h,kind,d,type,0x40U,hdr0,hdr1,special,
                UINT32_MAX,UINT32_MAX,caller,owner);
 if(kind){
  /* A tagged service cannot be passed to raw native async IPC without
   * first applying the same per-guest FSP routing/SM permissions as sync.
   * Execute the complete checked request on the caller thread and return
   * an already signaled kernel event as the async completion handle.
   * This is a synchronous compatibility fallback (not background I/O),
   * but preserves the observable buffer+event completion contract and
   * the real service result. No fake successful CMIF replies. */
  if(!out || !p || n<8){
    ipc_event_emit(call_id,4,h,kind,d,type,0x40U,hdr0,hdr1,special,
                   NX2_INVALID,UINT32_MAX,caller,owner);
    return NX2_INVALID;
  }
  *out=0;
  Handle server_event=0,client_event=0;
  uint32_t rc=svcCreateEvent(&server_event,&client_event);
  if(rc){
    ipc_event_emit(call_id,4,h,kind,d,type,0x40U,hdr0,hdr1,special,
                   rc,UINT32_MAX,caller,owner);
    return rc;
  }
  rc=filter_sync(p,n,h,1,caller);
  if(!rc)rc=svcSignalEvent(server_event);
  svcCloseHandle(server_event);
  if(rc){
    svcCloseHandle(client_event);
    ipc_event_emit(call_id,4,h,kind,d,type,0x40U,hdr0,hdr1,special,
                   rc,UINT32_MAX,caller,owner);
    return rc;
  }
  *out=client_event;
  ipc_event_emit(call_id,2,h,kind,d,type,0x40U,hdr0,hdr1,special,
                 0,UINT32_MAX,caller,owner);
  return 0;
 }
 uint32_t rc=nx2_raw_async(out,p,n,h);
 ipc_event_emit(call_id,2,h,kind,d,type,0x40U,hdr0,hdr1,special,
                rc,UINT32_MAX,caller,owner);
 return rc;
}

void nx2_exit_gate(void){nx2_rpc(RPC_ABORT,(uintptr_t)__builtin_return_address(0),0,0,0,0,"Game requested process exit; caller address in a",0,0);for(;;)__asm__ volatile("wfe");}

/* The original SDK chooses/maps its own virtual-address allocations. Keep the
 * kernel's real accounting and validation; only restrict these calls to the
 * genuine alias region and record early calls or failures. */
static uint32_t physical(uint64_t address,uint64_t bytes,int unmap){
 uint32_t active=__atomic_load_n(&nx2_native_vm_active,__ATOMIC_ACQUIRE);
 uint64_t begin=nx2_native_alias_begin,size=nx2_native_alias_bytes;
 if(!active||!bytes||((address|bytes)&4095)||address<begin||address-begin>size||bytes>size-(address-begin)){
  /* Reject only this guest SVC and keep the host alive. A legitimate
   * SDK allocator may recover or retry after an invalid alias request.
   * Never forward an out-of-region mapping into the host address space. */
  nx2_rpc(RPC_LOG,address,bytes,unmap,begin,0,
          "PHYSICAL_ALIAS_RANGE_REFUSED",0,0);
  return NX2_INVALID;
 }
 uint64_t *counter=unmap?&nx2_native_unmap_calls:&nx2_native_map_calls;
 uint64_t count=__atomic_add_fetch(counter,1,__ATOMIC_RELAXED);
 if(count<=16||!(count&255))nx2_rpc(RPC_PHYSICAL_MEMORY,address,bytes,UINT32_MAX,unmap,0,0,0,0);
 uint32_t rc=unmap?nx2_raw_unmap_physical(address,bytes):nx2_raw_map_physical(address,bytes);
 if(rc||count<=16||!(count&255))nx2_rpc(RPC_PHYSICAL_MEMORY,address,bytes,rc,unmap,0,0,0,0);
 return rc;
}
uint32_t nx2_map_physical(uint64_t address,uint64_t bytes){return physical(address,bytes,0);}
uint32_t nx2_unmap_physical(uint64_t address,uint64_t bytes){return physical(address,bytes,1);}
