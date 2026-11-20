/* SPDX-License-Identifier: GPL-2.0-only */
#include "vm_memory.h"
int nx2_plan_heap_release(const Nx2HeapPlan *p,const Nx2HeapView *v,Nx2ReleasePlan *out){
    if(!p||!v||!out)return 0;
    *out=(Nx2ReleasePlan){0};
    const uint64_t mask=NX2_GAME_ALIGNMENT-1;
    if(!p->original_begin||p->original_bytes>UINT64_MAX-p->original_begin||
       !v->heap_begin||v->heap_reserved_bytes>UINT64_MAX-v->heap_begin||
       v->tail_bytes>UINT64_MAX-v->tail_begin)return 0;
    const uint64_t end=p->original_begin+p->original_bytes,cut=p->game_begin;
    if(p->host_begin!=p->original_begin||p->host_bytes<NX2_HOST_MIN_BYTES||
       p->host_bytes>p->original_bytes||cut!=p->original_begin+p->host_bytes||
       cut<=v->heap_begin||cut>=end||p->original_begin<v->heap_begin||
       end>v->heap_begin+v->heap_reserved_bytes||
       (v->heap_begin&mask)||(cut&mask)||(end&mask)||
       end-cut<NX2_GAME_MIN_BYTES||p->game_bytes<NX2_GAME_MIN_BYTES||
       p->game_bytes>end-cut)return 0;
    /* _mallocInitialize consumes the newlib compatibility range by moving
     * fake_heap_start to fake_heap_end after initializing the page allocator.
     * Accept only its two exact lifecycle states, never an arbitrary cursor.
     * The queried plain heap region must reach the old committed end. */
    if((v->allocator_begin!=p->host_begin&&v->allocator_begin!=cut)||
       v->allocator_end!=cut||
       v->tail_begin>cut||v->tail_begin+v->tail_bytes!=end||
       v->tail_type!=5||v->tail_permission!=3||v->tail_attributes)return 0;
    out->retained_bytes=cut-v->heap_begin;
    out->released_begin=cut;
    out->released_bytes=end-cut;
    return 1;
}
