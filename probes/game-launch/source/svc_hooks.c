/* SPDX-License-Identifier: GPL-2.0-only */
#include "launch.h"
#include "ipc_policy.h"
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
   /* Preserve the real kernel's memory accounting. A bounded trace of
    * guest-visible queries also captures values after SetHeapSize, unlike
    * the old first-per-type log. The host prints all four RPC fields. */
   uint32_t rc=nx2_raw_info(out,type,handle,sub);
   if(l && out && handle==CUR_PROCESS_HANDLE && !sub &&
      (type==5 || type==6 || type==7 || type==16 ||
       type==21 || type==22)){
     uint64_t kernel_value=(!rc)?*out:0;
     /* The kernel reports the *host* process, including the reserved
      * launcher heap. A guest's UsedNonSystemMemorySize must not charge
      * the launcher-owned retained heap against the guest's own memory
      * budget. Only translate this accounting query; real GetInfo totals,
      * heap allocation, and physical memory remain kernel-governed.
      * Refuse to subtract unless all of the launcher-owned heap fits in
      * the reported usage. This is generic for any guest, not a game
      * ID/version-specific assertion bypass. */
     if(!rc && type==22 && nx2_process_heap_status==NX2_HEAP_READY &&
        nx2_process_heap.host_bytes &&
        kernel_value>=nx2_process_heap.host_bytes){
       *out=kernel_value-nx2_process_heap.host_bytes;
       nx2_rpc(RPC_LOG,kernel_value,*out,nx2_process_heap.host_bytes,type,0,
               "GUEST_MEMORY_ACCOUNTING_SUBTRACT_HOST",0,0);
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
#define NX2_NATIVE_PHYSICAL_FLOOR UINT64_C(0xC0000000) /* 3 GiB */
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
 MemoryInfo end_region,tail;u32 page=0;
 rc=svcQueryMemory(&end_region,&page,cut-1);
 if(R_FAILED(rc))return rc;
 rc=svcQueryMemory(&tail,&page,cut);
 if(R_FAILED(rc))return rc;
 uint64_t available=total>=used?total-used:0;
 uint64_t end_of_commit=end_region.addr+end_region.size;
 nx2_rpc(RPC_LOG,size,retain,end_of_commit,available,0,
         "NATIVE_HEAP_BOUNDS_INPUT",0,0);
 nx2_rpc(RPC_LOG,cut,tail.addr,tail.type,tail.perm,0,
         "NATIVE_HEAP_TAIL_INPUT",0,0);
 if(plan->original_bytes>UINT64_MAX-plan->original_begin ||
    plan->original_begin<base || cut<base || cut>original_end ||
    original_end<base || size<retain || size>original_end-base ||
    end_region.addr>cut-1 || end_region.size>UINT64_MAX-end_region.addr ||
    end_of_commit!=cut || end_region.type!=MemType_Heap ||
    (end_region.perm&Perm_Rw)!=Perm_Rw ||
    tail.type!=MemType_Unmapped || tail.perm!=Perm_None) {
   nx2_rpc(RPC_LOG,size,retain,end_of_commit,tail.type,0,
           "NATIVE_HEAP_BOUNDS_REFUSED",0,0);
   return 0xd001;
 }
 uint64_t increase=size>retain?size-retain:0;
 if(increase>available || available-increase<NX2_NATIVE_PHYSICAL_FLOOR) {
   nx2_rpc(RPC_LOG,size,retain,available,NX2_NATIVE_PHYSICAL_FLOOR,0,
           "NATIVE_HEAP_BUDGET_REFUSED",0,0);
   return 0xd001;
 }
 void *returned=NULL;
 rc=svcSetHeapSize(&returned,size);
 nx2_rpc(RPC_LOG,size,retain,available,rc,0,
         "NATIVE_HEAP_SET_RESULT",0,0);
 if(R_FAILED(rc))return rc;
 if((uintptr_t)returned!=base) {
   nx2_rpc(RPC_ABORT,(uintptr_t)returned,base,size,0,0,
           "Native heap base changed unexpectedly",0,0);
   for(;;)__asm__ volatile("wfe");
 }
 *out=returned;
 __atomic_store_n(&l->heap_size,size,__ATOMIC_RELEASE);
 return 0;
}
uint32_t nx2_rtld_query(void *memory,uint32_t *page,uint64_t address){Nx2Launch *l=nx2_launch;uint64_t begin=0,end=UINT64_C(0x8000000000);uint32_t type=0,perm=0;for(uint32_t i=0;i<l->region_count;i++){Nx2Region *r=&l->regions[i];if(address>=r->begin&&address<r->end){begin=r->begin;end=r->end;type=r->type;perm=r->permission;break;}if(r->end<=address&&r->end>begin)begin=r->end;if(r->begin>address&&r->begin<end)end=r->begin;}if(!memory)return 0xdc01;if(address>=UINT64_C(0x8000000000)){begin=UINT64_C(0x8000000000);end=0;type=0x10;perm=0;}uint64_t *q=memory;uint32_t *w=memory;q[0]=begin;q[1]=end-begin;w[4]=type;w[5]=0;w[6]=perm;w[7]=w[8]=w[9]=0;if(page)*page=0;return 0;}
static uint32_t tag(uint32_t h){for(unsigned i=0;i<128;i++){uint64_t v=__atomic_load_n(&nx2_launch->handle_tags[i],__ATOMIC_ACQUIRE);if((uint32_t)v==h&&h)return v>>32;}return 0;}
static void set_tag(uint32_t h,uint32_t kind){if(!h)return;for(unsigned i=0;i<128;i++){uint64_t v=__atomic_load_n(&nx2_launch->handle_tags[i],__ATOMIC_RELAXED);if((uint32_t)v==h){__atomic_store_n(&nx2_launch->handle_tags[i],kind?((uint64_t)kind<<32)|h:0,__ATOMIC_RELEASE);return;}}if(!kind)return;for(unsigned i=0;i<128;i++){uint64_t expected=0;if(__atomic_compare_exchange_n(&nx2_launch->handle_tags[i],&expected,((uint64_t)kind<<32)|h,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED))return;}nx2_rpc(RPC_ABORT,h,0,0,0,0,"Service handle tag table full",0,0);for(;;)__asm__ volatile("wfe");}
uint32_t nx2_connect(uint32_t *out,const char *name){nx2_rpc(RPC_LOG,0x1f,0,0,0,0,"CONNECT_TO_NAMED_PORT_BEGIN",name,0);uint32_t rc=nx2_raw_connect(out,name);if(!rc&&name&&name[0]=='s'&&name[1]=='m'&&name[2]==':'&&!name[3])set_tag(*out,1);nx2_rpc(RPC_LOG,rc,rc?0:*out,0x1f,0,0,"CONNECT_TO_NAMED_PORT_RESULT",name,0);return rc;}
static uint32_t word(const unsigned char *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void response_tags(const unsigned char *p,uint64_t n,uint32_t kind){if(n<12||!(word(p+4)&0x80000000))return;uint32_t s=word(p+8),copy=(s>>1)&15,move=(s>>5)&15;uint64_t off=12+((s&1)?8:0);if(off+4*(copy+move)>n)return;for(uint32_t i=0;i<copy+move;i++)set_tag(word(p+off+4*i),kind);}
static uint32_t filter_sync(void *buffer,uint64_t n,uint32_t h,int external){
 unsigned char *p=buffer;uint32_t kind=tag(h);Nx2IpcDecision d=nx2_ipc_decide(p,n,kind);uint32_t cmd=d.command,has=d.valid,control=d.control,want_fsp=d.want_fsp;uint64_t scan=n<256?n:256;uint32_t request_type=n>=4?word(p)&65535:0;char service[9]={0};if(kind==1)nx2_ipc_service_name(p,n,service);
 if(kind==2&&!nx2_ipc_fsp_allowed(d)){__atomic_store_n(&nx2_launch->fsp_denied,1,__ATOMIC_RELEASE);nx2_rpc(RPC_ABORT,h,cmd,has,control,0,"Blocked unapproved native FSP request",0,0);for(;;)__asm__ volatile("wfe");}
 if(kind)nx2_rpc(RPC_IPC,h,cmd,UINT64_MAX,kind,0,"IPC_BEGIN",service,0);
 uint32_t rc=external?nx2_raw_buffer(buffer,n,h):nx2_raw_sync(h);
 uint32_t service_result=0;if(kind&&!rc&&nx2_ipc_reply_result(p,n,request_type,&service_result))nx2_rpc(RPC_LOG,service_result,cmd,h,kind,0,"SERVICE_REPLY_RESULT",service,0);
 if(!rc){if(want_fsp)response_tags(p,scan,2);else if(kind&&control&&(cmd==1||cmd==2||cmd==4))response_tags(p,scan,kind);}
 if(kind||rc)nx2_rpc(RPC_IPC,h,cmd,rc,kind,0,want_fsp?"fsp-srv service acquired":(kind==2?"FSP request":"IPC"),0,0);
 return rc;
}
uint32_t nx2_sync(uint32_t h){void *tlr;__asm__ volatile("mrs %0,tpidrro_el0":"=r"(tlr));return filter_sync(tlr,256,h,0);}
uint32_t nx2_sync_buffer(void *p,uint64_t n,uint32_t h){return filter_sync(p,n,h,1);}
uint32_t nx2_close_handle(uint32_t h){uint32_t rc=nx2_raw_close(h);if(!rc)set_tag(h,0);return rc;}
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

uint32_t nx2_async(uint32_t *out,void *p,uint64_t n,uint32_t h){if(tag(h)){nx2_rpc(RPC_ABORT,h,0,0,0,0,"Refused asynchronous SM/FSP operation",0,0);for(;;)__asm__ volatile("wfe");}return nx2_raw_async(out,p,n,h);}

void nx2_exit_gate(void){nx2_rpc(RPC_ABORT,(uintptr_t)__builtin_return_address(0),0,0,0,0,"Game requested process exit; caller address in a",0,0);for(;;)__asm__ volatile("wfe");}

/* The original SDK chooses/maps its own virtual-address allocations. Keep the
 * kernel's real accounting and validation; only restrict these calls to the
 * genuine alias region and record early calls or failures. */
static uint32_t physical(uint64_t address,uint64_t bytes,int unmap){
 uint32_t active=__atomic_load_n(&nx2_native_vm_active,__ATOMIC_ACQUIRE);
 uint64_t begin=nx2_native_alias_begin,size=nx2_native_alias_bytes;
 if(!active||!bytes||((address|bytes)&4095)||address<begin||address-begin>size||bytes>size-(address-begin)){
  nx2_rpc(RPC_ABORT,address,bytes,unmap,0,0,"Physical-memory request outside active native alias region",0,0);
  for(;;)__asm__ volatile("wfe");
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
