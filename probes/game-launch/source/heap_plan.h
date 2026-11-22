/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>

enum { NX2_HEAP_READY=1, NX2_HEAP_TOO_SMALL, NX2_HEAP_INVALID };
/* Allow the guest to borrow unused title-override heap instead of
 * permanently reserving 1 GiB for the homebrew host. The actual host
 * allocator is limited to this committed prefix from process startup.
 * Every game still passes real VM bounds and physical-budget checks. */
#define NX2_HOST_MIN_BYTES UINT64_C(0x20000000) /* 512 MiB */
#define NX2_GAME_MIN_BYTES UINT64_C(0x100000000) /* 4 GiB */
#define NX2_GAME_MAX_BYTES UINT64_C(0x300000000) /* 12 GiB cap, not a promise */
#define NX2_GAME_ALIGNMENT UINT64_C(0x200000)
/* Leave real, uncommitted physical headroom for GPU/host use.
 * This is an experimental safety floor, never a simulated allocation. */
#define NX2_NATIVE_PHYSICAL_FLOOR_BYTES UINT64_C(0x18000000) /* 384 MiB */

typedef struct {
    uint64_t original_begin, original_bytes;
    uint64_t host_begin, host_bytes;
    uint64_t game_begin, game_bytes;
} Nx2HeapPlan;

/* Pure range planning; never allocates or accesses the described memory. */
uint32_t nx2_heap_plan(uint64_t begin, uint64_t bytes, Nx2HeapPlan *out);
extern Nx2HeapPlan nx2_process_heap;
extern uint32_t nx2_process_heap_status;

/* True for shrink/equal; growing must leave real physical headroom. */
int nx2_heap_growth_allowed(uint64_t,uint64_t,uint64_t,uint64_t);
