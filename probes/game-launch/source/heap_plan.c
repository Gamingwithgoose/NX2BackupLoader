/* SPDX-License-Identifier: GPL-2.0-only */
#include "heap_plan.h"

uint32_t nx2_heap_plan(uint64_t begin, uint64_t bytes, Nx2HeapPlan *out) {
    if (!out) return NX2_HEAP_INVALID;
    *out = (Nx2HeapPlan){0};
    if (!begin || !bytes || bytes > UINT64_MAX-begin) return NX2_HEAP_INVALID;
    out->original_begin = out->host_begin = begin;
    out->original_bytes = out->host_bytes = bytes;
    if (bytes < NX2_HOST_MIN_BYTES) return NX2_HEAP_TOO_SMALL;
    const uint64_t mask = NX2_GAME_ALIGNMENT-1;
    const uint64_t end = (begin+bytes)&~mask;
    const uint64_t minimum_begin = begin+NX2_HOST_MIN_BYTES;
    if (minimum_begin > UINT64_MAX-mask) return NX2_HEAP_TOO_SMALL;
    const uint64_t earliest = (minimum_begin+mask)&~mask;
    if (end < earliest || end-earliest < NX2_GAME_MIN_BYTES) return NX2_HEAP_TOO_SMALL;
    uint64_t capacity = end-earliest;
    if (capacity > NX2_GAME_MAX_BYTES) capacity = NX2_GAME_MAX_BYTES;
    out->game_begin = end-capacity;
    out->game_bytes = capacity;
    out->host_bytes = out->game_begin-begin;
    return NX2_HEAP_READY;
}

/* Preserve actual kernel resource accounting, but only charge the physical
 * headroom guard when the guest is asking for MORE committed heap pages.
 * An unchanged or smaller heap request cannot consume additional pages. */
int nx2_heap_growth_allowed(uint64_t old_size,uint64_t requested_size,
                            uint64_t free_bytes,uint64_t physical_floor){
    if(requested_size<=old_size)return 1;
    uint64_t growth=requested_size-old_size;
    return growth<=free_bytes &&
           free_bytes-growth>=physical_floor;
}

/* The SDK may size its requested heap as TotalNonSystemMemorySize minus
 * UsedNonSystemMemorySize. Both queries must tell the same bounded story.
 * Reserve real physical headroom and clamp to the actual unmapped guest
 * heap span. Never turn this into an allocation or a fake success. */
int nx2_guest_memory_budget(uint64_t kernel_total,uint64_t kernel_used,
                            uint64_t physical_free,uint64_t host_prefix,
                            uint64_t guest_capacity,uint64_t guest_committed,
                            uint64_t physical_floor,uint64_t *reported_total,
                            uint64_t *reported_used){
    if(!reported_total||!reported_used||kernel_used>kernel_total||
       guest_committed>guest_capacity)return 0;
    uint64_t used=kernel_used>=host_prefix?kernel_used-host_prefix:0;
    uint64_t free_budget=kernel_total-kernel_used;
    if(free_budget>physical_free)free_budget=physical_free;
    free_budget=free_budget>physical_floor?free_budget-physical_floor:0;
    uint64_t guest_tail=guest_capacity-guest_committed;
    if(free_budget>guest_tail)free_budget=guest_tail;
    if(used>UINT64_MAX-free_budget)return 0;
    *reported_used=used;
    *reported_total=used+free_budget;
    return 1;
}
