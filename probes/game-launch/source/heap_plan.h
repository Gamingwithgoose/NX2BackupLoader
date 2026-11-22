/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>

enum { NX2_HEAP_READY=1, NX2_HEAP_TOO_SMALL, NX2_HEAP_INVALID };
/* A 512 MiB committed host prefix is insufficient for large multi-module
 * ExeFS images: every mapped NSO retains its real backing pages while later
 * modules are still being decoded. Reserve 768 MiB for BOTH NUP and folder
 * loading, without returning to the former 1 GiB fixed host reservation.
 * The guest still gets actual bounded non-system-memory accounting and
 * real physical reserve checks; do not fabricate guest heap success. */
#define NX2_HOST_MIN_BYTES UINT64_C(0x30000000) /* 768 MiB */
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

/* The guest shares a host process. Its visible non-system-memory budget
 * must describe what its private heap could really grow to, not every
 * physically free page belonging to the host title. Outputs describe a
 * coherent TotalNonSystemMemorySize / UsedNonSystemMemorySize pair.
 * Pure arithmetic for portable regression tests. */
int nx2_guest_memory_budget(uint64_t kernel_total,uint64_t kernel_used,
                            uint64_t physical_free,uint64_t host_prefix,
                            uint64_t guest_capacity,uint64_t guest_committed,
                            uint64_t physical_floor,uint64_t *reported_total,
                            uint64_t *reported_used);
