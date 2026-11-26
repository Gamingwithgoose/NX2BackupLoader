/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
/* Set before preparation; immutable after guest handoff. */
extern uint32_t nx2_native_compat;
extern uint32_t nx2_detailed_trace;
extern uint32_t nx2_minimal_logging;
void nx2_enter_guest_stack(void *launch, void *stack_top) __attribute__((noreturn));
