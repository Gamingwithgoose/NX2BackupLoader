/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include <stddef.h>
int nx2_patch_gate(unsigned char *,uint32_t,uint32_t,uint32_t *,uint32_t,uintptr_t,uintptr_t,uintptr_t *);
const char *nx2_patch_failure_reason(void);
const char *nx2_patch_method(void);

/* Redirect a nonreturning svcBreak instruction to a single executable
 * trampoline already installed for the same module. Only the exact SVC
 * instruction is overwritten; nothing after it is modified. */
int nx2_patch_shared_break(unsigned char *text, uint32_t text_size,
                           uint32_t padded_size, uint32_t site,
                           uint32_t trampoline);

/* Same safe 4-byte patch as the shared Break gate, but for direct
 * nonreturning SVC #7 (ExitProcess) within any guest NSO. */
int nx2_patch_shared_exit(unsigned char *text,uint32_t text_size,
                          uint32_t padded_size,uint32_t site,
                          uint32_t trampoline);
