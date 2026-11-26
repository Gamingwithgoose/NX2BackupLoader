/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include <stddef.h>
/* Bytes of reserved virtual address space, not committed RAM.
 * 1: valid field (including zero), 0: absent, -1: malformed/unsupported. */
int nx2_heapdef_reserve(const void *,size_t,uint64_t *);
