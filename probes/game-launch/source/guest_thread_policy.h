/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>

/* Do not shrink the proven 1 MiB guest baseline. Honor larger NPDM main
 * thread stack requests without an artificial 16 MiB limit. The returned
 * stack is page-aligned; the real host allocator and kernel cap usage. */
#define NX2_GUEST_STACK_BASE UINT32_C(0x100000)
int nx2_guest_main_stack(uint32_t declared_bytes,uint32_t *allocated_bytes);
