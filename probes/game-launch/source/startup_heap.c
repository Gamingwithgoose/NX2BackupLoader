/* SPDX-License-Identifier: GPL-2.0-only */
#include <switch.h>
#include "heap_plan.h"

Nx2HeapPlan nx2_process_heap;
uint32_t nx2_process_heap_status;

/* libounce calls this before thread/argv/malloc initialization. Its allocator
 * subsequently reads fake_heap_start/end. Keep the game reservation outside
 * those bounds from the outset; it must never be handed to free(). */
void __libnx_initheap(void) {
    void *base = NULL;
    uint64_t bytes;
    if (envHasHeapOverride()) {
        base = envGetHeapOverrideAddr();
        bytes = envGetHeapOverrideSize();
    } else {
        /* A small normal host heap keeps the UI usable without a title override.
         * The plan will explicitly refuse a game reservation in this case. */
        bytes = UINT64_C(0x20000000);
        Result rc = svcSetHeapSize(&base, bytes);
        if (R_FAILED(rc)) diagAbortWithResult(rc);
    }
    nx2_process_heap_status = nx2_heap_plan((uintptr_t)base, bytes, &nx2_process_heap);
    if (nx2_process_heap_status == NX2_HEAP_INVALID) diagAbortWithResult(0x1f59);
    extern char *fake_heap_start, *fake_heap_end;
    fake_heap_start = (char *)(uintptr_t)nx2_process_heap.host_begin;
    fake_heap_end = fake_heap_start+nx2_process_heap.host_bytes;
}
