/* SPDX-License-Identifier: GPL-2.0-only */
#include <switch.h>
#include <switch2/memory/heap.h>
#include <stdarg.h>
#include "vm_release.h"
#include "log_policy.h"

static void row(FILE *f,const char *fmt,...){
    if(!f||!nx2_log_keep(fmt))return;
    va_list a;va_start(a,fmt);vfprintf(f,fmt,a);va_end(a);fflush(f);
}
int nx2_release_heap_for_vm(FILE *f,const Nx2HeapPlan *p,uint64_t min_resource,
                           uint64_t min_alias,uint64_t min_free,uint64_t *budget){
    uint64_t heap=0,reserved=0,resource=0,alias=0,alias_size=0,total=0,used=0;
    const unsigned kinds[]={4,5,16,2,3,6,7};
    uint64_t *outputs[]={&heap,&reserved,&resource,&alias,&alias_size,&total,&used};
    if(!f||!p||!budget||__atomic_load_n(&nx2_native_vm_active,__ATOMIC_ACQUIRE))return 0;
    *budget=0;
    for(unsigned i=0;i<7;i++){
        Result rc=svcGetInfo(outputs[i],kinds[i],CUR_PROCESS_HANDLE,0);
        row(f,"VM_HOST_INFO type=%u result=%08x value=%016llx\n",kinds[i],rc,(unsigned long long)*outputs[i]);
        if(R_FAILED(rc))return 0;
    }
    /* Recheck after NSO mapping: this remains the host process's genuine
     * kernel-managed memory-management reservation, not free application
     * RAM. A shortage is different from an unavailable alias region. */
    if(resource<min_resource){
        /* The NPDM reservation is a process memory-manager setting, not
         * the physical-memory budget checked below. On zero-reservation
         * hosts the real kernel may refuse physical mappings; that
         * decision belongs to svcMapPhysicalMemory at first use, not to
         * an unconditional host-identity/NPDM comparison. Do NOT skip
         * heap ownership, alias bounds, free physical RAM or kernel RCs. */
        row(f,"WARNING_VM_HOST_RESOURCE_DIFFERENCE available_bytes=%llu guest_npdm_bytes=%llu deficit_bytes=%llu policy=kernel_enforced\n",
            (unsigned long long)resource,(unsigned long long)min_resource,
            (unsigned long long)(min_resource-resource));
    }
    if(alias_size<min_alias||!alias||alias_size>UINT64_MAX-alias){
        row(f,"VM_REFUSED invalid_alias_capacity alias_base=%016llx alias_bytes=%llu min_bytes=%llu\n",
            (unsigned long long)alias,(unsigned long long)alias_size,
            (unsigned long long)min_alias);
        return 0;
    }
    MemoryInfo tail;u32 page=0;Result rc=svcQueryMemory(&tail,&page,p->game_begin);
    if(R_FAILED(rc)){row(f,"VM_TAIL_QUERY result=%08x\n",rc);return 0;}
    extern char *fake_heap_start,*fake_heap_end;
    Nx2HeapView view={heap,reserved,tail.addr,tail.size,tail.type,tail.perm,tail.attr,
                     (uintptr_t)fake_heap_start,(uintptr_t)fake_heap_end};
    Nx2ReleasePlan plan;
    row(f,"VM_TAIL_QUERY result=%08x base=%016llx size=%llu type=%x permission=%u attributes=%x\n",
        rc,(unsigned long long)tail.addr,(unsigned long long)tail.size,tail.type,tail.perm,tail.attr);
    row(f,"VM_ALLOCATOR_BOUNDS compatibility_begin=%016llx compatibility_end=%016llx planned_begin=%016llx planned_end=%016llx allocator_total=%llu host_bytes=%llu\n",
        (unsigned long long)view.allocator_begin,(unsigned long long)view.allocator_end,
        (unsigned long long)p->host_begin,(unsigned long long)p->game_begin,
        (unsigned long long)heapGetTotalSize(),(unsigned long long)p->host_bytes);
    if(!nx2_plan_heap_release(p,&view,&plan)||heapGetTotalSize()>p->host_bytes){
        row(f,"VM_REFUSED heap/allocator/unused-tail ownership checks failed\n");return 0;
    }
    row(f,"VM_HEAP_RELEASE_BEGIN heap_base=%016llx retain_bytes=%llu release_base=%016llx release_bytes=%llu allocator_end=%016llx\n",
        (unsigned long long)heap,(unsigned long long)plan.retained_bytes,
        (unsigned long long)plan.released_begin,(unsigned long long)plan.released_bytes,
        (unsigned long long)(uintptr_t)fake_heap_end);
    void *returned=NULL;rc=svcSetHeapSize(&returned,plan.retained_bytes);
    row(f,"VM_HEAP_RELEASE_RESULT result=%08x returned_base=%016llx\n",rc,(unsigned long long)(uintptr_t)returned);
    if(R_FAILED(rc)||(uintptr_t)returned!=heap)return 0;
    /* The tail is no longer an allocation. Never clear it, expose it as a
     * private heap, or give it to free(). The kernel owns it again. */
    MemoryInfo after;
    rc=svcQueryMemory(&after,&page,plan.released_begin);
    row(f,"VM_RELEASED_TAIL result=%08x type=%x permission=%u\n",rc,R_SUCCEEDED(rc)?after.type:0,R_SUCCEEDED(rc)?after.perm:0);
    if(R_FAILED(rc)||after.type!=MemType_Unmapped||after.perm!=Perm_None)return 0;
    rc=svcQueryMemory(&after,&page,plan.released_begin-1);
    if(R_FAILED(rc)||(after.perm&Perm_Rw)!=Perm_Rw)return 0;
    Result a=svcGetInfo(&total,6,CUR_PROCESS_HANDLE,0),b=svcGetInfo(&used,7,CUR_PROCESS_HANDLE,0);
    uint64_t free_bytes=R_SUCCEEDED(a)&&R_SUCCEEDED(b)&&total>=used?total-used:0;
    row(f,"VM_PHYSICAL_BUDGET total=%llu used=%llu available=%llu\n",
        (unsigned long long)total,(unsigned long long)used,(unsigned long long)free_bytes);
    if(R_FAILED(a)||R_FAILED(b)||free_bytes<min_free)return 0;
    nx2_native_alias_begin=alias;nx2_native_alias_bytes=alias_size;
    __atomic_store_n(&nx2_native_vm_active,1,__ATOMIC_RELEASE);
    *budget=free_bytes;
    row(f,"VM_BACKEND_READY real kernel accounting; original SDK maps physical memory\n");
    return 1;
}
