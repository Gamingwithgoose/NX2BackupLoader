/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
enum {
  NX2_TRACE_LAUNCH_SELECTED=1,
  NX2_TRACE_HOST_APPROVED=2,
  NX2_TRACE_MODULE_READY=3,
  NX2_TRACE_GUEST_START=4,
  NX2_TRACE_GUEST_HEARTBEAT=5,
  NX2_TRACE_RUN_ENDED=6,
  NX2_TRACE_PREPARE_FAILED=7,
  NX2_TRACE_CPU_EXCEPTION_DESC=8,
  NX2_TRACE_CPU_EXCEPTION_PC=9,
  NX2_TRACE_CPU_EXCEPTION_LR=10,
  NX2_TRACE_CPU_EXCEPTION_FAR=11,
  NX2_TRACE_CPU_EXCEPTION_ESR=12
};
#ifdef NX2_DEV_TRACE
void nx2_trace_begin(uint64_t guest_id);
void nx2_trace_event(unsigned event, uint64_t value);
#else
static inline void nx2_trace_begin(uint64_t guest_id) {(void)guest_id;}
static inline void nx2_trace_event(unsigned event, uint64_t value) {(void)event;(void)value;}
#endif
