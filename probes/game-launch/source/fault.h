/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <switch.h>
typedef struct {
    uint32_t state;
    ThreadExceptionDump dump;
    uint64_t tpidr, tlr;
} Nx2Fault;
extern Nx2Fault nx2_fault;
/* Called on the faulted thread. Only copies state; never calls host libraries. */
void __libnx_exception_handler(ThreadExceptionDump *);
