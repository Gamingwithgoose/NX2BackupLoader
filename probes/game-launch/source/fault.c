/* SPDX-License-Identifier: GPL-2.0-only */
#include "fault.h"
#include "raw_fault.h"

Nx2Fault nx2_fault;
Nx2RawFault nx2_raw_fault __attribute__((aligned(16)));
/* Use libounce's existing exception entry/context decoder and a separate stack.
 * The handler parks the faulted thread; the healthy host loop writes the report
 * and ends the process without resuming or unmapping faulted game code. */
u32 __nx_exception_ignoredebug = 1;
u64 __nx_exception_stack_size = 0x8000;
unsigned char __nx_exception_stack[0x8000] __attribute__((aligned(16)));

void __attribute__((noreturn)) __libnx_exception_handler(ThreadExceptionDump *dump) {
    uint32_t empty=0;
    if (dump && __atomic_compare_exchange_n(&nx2_fault.state,&empty,1,0,
                                           __ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) {
        const volatile unsigned char *source=(const volatile unsigned char *)dump;
        unsigned char *dest=(unsigned char *)&nx2_fault.dump;
        for (unsigned i=0;i<sizeof(*dump);i++) dest[i]=source[i];
        __asm__ volatile("mrs %0,tpidr_el0":"=r"(nx2_fault.tpidr));
        __asm__ volatile("mrs %0,tpidrro_el0":"=r"(nx2_fault.tlr));
        __atomic_store_n(&nx2_fault.state,2,__ATOMIC_RELEASE);
        __asm__ volatile("sev" ::: "memory");
    }
    for (;;) __asm__ volatile("wfe" ::: "memory");
}
