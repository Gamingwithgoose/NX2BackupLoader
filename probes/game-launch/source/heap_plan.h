/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>

enum { NX2_HEAP_READY=1, NX2_HEAP_TOO_SMALL, NX2_HEAP_INVALID };
#define NX2_HOST_MIN_BYTES UINT64_C(0x40000000)
#define NX2_GAME_MIN_BYTES UINT64_C(0x100000000)
#define NX2_GAME_MAX_BYTES UINT64_C(0x200000000)
#define NX2_GAME_ALIGNMENT UINT64_C(0x200000)

typedef struct {
    uint64_t original_begin, original_bytes;
    uint64_t host_begin, host_bytes;
    uint64_t game_begin, game_bytes;
} Nx2HeapPlan;

/* Pure range planning; never allocates or accesses the described memory. */
uint32_t nx2_heap_plan(uint64_t begin, uint64_t bytes, Nx2HeapPlan *out);
extern Nx2HeapPlan nx2_process_heap;
extern uint32_t nx2_process_heap_status;
