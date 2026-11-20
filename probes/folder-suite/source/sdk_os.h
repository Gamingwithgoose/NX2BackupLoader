/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
    void (*initialize)(uint32_t, const void *);
    void *(*current_thread)(void);
    uint64_t (*thread_id)(const void *);
    uint32_t (*allocate_tls)(uint32_t *, uintptr_t);
    uint64_t (*get_tls)(uint32_t);
    void (*set_tls)(uint32_t, uint64_t);
    void (*free_tls)(uint32_t);
    void (*initialize_mutex)(void *, int, int);
    int (*try_lock_mutex)(void *);
    void (*unlock_mutex)(void *);
    void (*finalize_mutex)(void *);
} Nx2SdkOsApi;

/* Fixed-width fields also let the emulator exercise this actual worker binary. */
typedef struct {
    Nx2SdkOsApi api;
    uint64_t handle, data_begin, data_end;
    uint64_t stage, trace_count, trace[32];
    uint64_t initialized, thread, thread_id, thread_valid;
    uint64_t tls_result, tls_slot, tls_initial, tls_value, tls_cleared, tls_freed;
    uint64_t mutex_first, mutex_second, mutex_third, mutex_finalized;
    uint64_t tail_restored, tp_restored, preemption_same, context_fault;
    _Alignas(16) unsigned char mutex[64];
} Nx2SdkOsJob;

#define NX2_SDK_TLS_COOKIE UINT64_C(0x1122334455667788)
void nx2_sdk_os_run(Nx2SdkOsJob *job);
