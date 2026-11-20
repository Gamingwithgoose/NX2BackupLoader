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
