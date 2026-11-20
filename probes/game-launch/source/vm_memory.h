/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "heap_plan.h"
typedef struct {
    uint64_t heap_begin, heap_reserved_bytes;
    uint64_t tail_begin, tail_bytes, tail_type, tail_permission, tail_attributes;
    uint64_t allocator_begin, allocator_end;
} Nx2HeapView;
typedef struct {uint64_t retained_bytes, released_begin, released_bytes;} Nx2ReleasePlan;
int nx2_plan_heap_release(const Nx2HeapPlan *,const Nx2HeapView *,Nx2ReleasePlan *);
extern uint32_t nx2_native_vm_active;
extern uint64_t nx2_native_alias_begin, nx2_native_alias_bytes;
extern uint64_t nx2_native_map_calls, nx2_native_unmap_calls;
