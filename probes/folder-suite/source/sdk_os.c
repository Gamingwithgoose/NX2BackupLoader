/* SPDX-License-Identifier: GPL-2.0-only */
#include "sdk_os.h"

static void checkpoint(Nx2SdkOsJob *job, uint64_t stage) {
    if (job->trace_count < 32) job->trace[job->trace_count++] = stage;
    __atomic_store_n(&job->stage, stage, __ATOMIC_RELEASE);
}

/* No libounce, libc, logging, allocation or host TLS calls may occur between
 * initialize() and restoration. The caller owns a dedicated joined thread. */
void nx2_sdk_os_run(Nx2SdkOsJob *job) {
    volatile unsigned char *tlr;
    uintptr_t original_tp, current_tp;
    unsigned char saved[128];
    __asm__ volatile("mrs %0,tpidrro_el0" : "=r"(tlr));
    __asm__ volatile("mrs %0,tpidr_el0" : "=r"(original_tp));
    const uint16_t before = *(volatile uint16_t *)(tlr + 0x100);
    if (before) {
        job->context_fault = 1;
        checkpoint(job, 99);
        return;
    }
    for (unsigned i = 0; i < sizeof(saved); i++) saved[i] = tlr[0x180 + i];
    for (unsigned i = 0; i < sizeof(saved); i++) tlr[0x180 + i] = 0;
    __asm__ volatile("msr tpidr_el0,xzr\n\tisb" ::: "memory");

    checkpoint(job, 1);
    job->api.initialize((uint32_t)job->handle, NULL);
    job->initialized = 1;
    checkpoint(job, 2);
    job->thread = (uintptr_t)job->api.current_thread();
    if (job->thread < job->data_begin || job->thread > job->data_end ||
        job->data_end - job->thread < 0x1c0) goto restore;
    job->thread_valid = 1;
    job->thread_id = job->api.thread_id((const void *)(uintptr_t)job->thread);

    checkpoint(job, 3);
    job->tls_slot = 0;
    job->tls_result = job->api.allocate_tls((uint32_t *)&job->tls_slot, 0);
    if (!job->tls_result) {
        job->tls_initial = job->api.get_tls((uint32_t)job->tls_slot);
        job->api.set_tls((uint32_t)job->tls_slot, NX2_SDK_TLS_COOKIE);
        job->tls_value = job->api.get_tls((uint32_t)job->tls_slot);
        job->api.set_tls((uint32_t)job->tls_slot, 0);
        job->tls_cleared = job->api.get_tls((uint32_t)job->tls_slot) == 0;
        job->api.free_tls((uint32_t)job->tls_slot);
        job->tls_freed = 1;
    }

    checkpoint(job, 4);
    job->api.initialize_mutex(job->mutex, 0, 0);
    job->mutex_first = job->api.try_lock_mutex(job->mutex);
    if (job->mutex_first) {
        job->mutex_second = job->api.try_lock_mutex(job->mutex);
        if (!job->mutex_second) {
            job->api.unlock_mutex(job->mutex);
            job->mutex_third = job->api.try_lock_mutex(job->mutex);
            if (job->mutex_third) {
                job->api.unlock_mutex(job->mutex);
                job->api.finalize_mutex(job->mutex);
                job->mutex_finalized = 1;
            }
        }
    }

restore:
    checkpoint(job, 5);
    /* Preserve kernel-maintained data outside the SDK-owned 0x180..0x1ff tail. */
    job->preemption_same = *(volatile uint16_t *)(tlr + 0x100) == before;
    for (unsigned i = 0; i < sizeof(saved); i++) tlr[0x180 + i] = saved[i];
    __asm__ volatile("msr tpidr_el0,%0\n\tisb" :: "r"(original_tp) : "memory");
    job->tail_restored = 1;
    for (unsigned i = 0; i < sizeof(saved); i++)
        if (tlr[0x180 + i] != saved[i]) job->tail_restored = 0;
    __asm__ volatile("mrs %0,tpidr_el0" : "=r"(current_tp));
    job->tp_restored = current_tp == original_tp;
    job->context_fault = !job->preemption_same || !job->tail_restored || !job->tp_restored;
    checkpoint(job, job->context_fault ? 99 : 6);
}

/* Test metadata contains offsets only, never decrypted Nintendo code. */
const uint64_t nx2_sdk_os_job_layout[] = {
    sizeof(Nx2SdkOsJob), offsetof(Nx2SdkOsJob, handle),
    offsetof(Nx2SdkOsJob, data_begin), offsetof(Nx2SdkOsJob, stage),
    offsetof(Nx2SdkOsJob, initialized), offsetof(Nx2SdkOsJob, tail_restored)
};
