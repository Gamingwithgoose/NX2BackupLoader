/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>

/* Do not shrink the proven 1 MiB guest baseline. Honor larger NPDM main
 * thread stacks up to a bounded 16 MiB to avoid overflowing host memory.
 * The returned stack is page-aligned for libounce threadCreate. */
#define NX2_GUEST_STACK_BASE UINT32_C(0x100000)
#define NX2_GUEST_STACK_CAP  UINT32_C(0x1000000)
int nx2_guest_main_stack(uint32_t declared_bytes,uint32_t *allocated_bytes);
