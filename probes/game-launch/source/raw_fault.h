/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#define NX2_RAW_KIND_OFFSET 8
#define NX2_RAW_CONTEXT_OFFSET 16
#define NX2_RAW_SP_OFFSET 24
#define NX2_RAW_TICK_OFFSET 32
#define NX2_RAW_FRAME_OFFSET 40
#define NX2_RAW_GPRS_OFFSET 160
#define NX2_RAW_ENTRY_LR_OFFSET 328
/* Switch2 counter31.25MHz, per libounce arm/counter.h. Fault path only. */
#define NX2_RAW_GRACE_TICKS 3125000
#ifndef __ASSEMBLER__
#include <stdint.h>
#include <stddef.h>
typedef struct {
  uint32_t state,context_valid; /*0 empty,1 writer,2 published,3 host persisted*/
  uint64_t kind,context,entry_sp,tick;
  uint64_t frame[15]; /* x0..8,LR,SP,PC,PSTATE/AFSR0,AFSR1/ESR,FAR */
  uint64_t gpr9_29[21];
  uint64_t entry_lr;
} Nx2RawFault;
extern Nx2RawFault nx2_raw_fault;
extern uint32_t nx2_raw_fault_trace_result;
uint32_t nx2_raw_fault_trace_open(uint64_t host,uint64_t guest);
void nx2_raw_fault_trace_write(void);
_Static_assert(offsetof(Nx2RawFault,kind)==NX2_RAW_KIND_OFFSET,"raw kind ABI");
_Static_assert(offsetof(Nx2RawFault,context)==NX2_RAW_CONTEXT_OFFSET,"raw context ABI");
_Static_assert(offsetof(Nx2RawFault,entry_sp)==NX2_RAW_SP_OFFSET,"raw SP ABI");
_Static_assert(offsetof(Nx2RawFault,tick)==NX2_RAW_TICK_OFFSET,"raw tick ABI");
_Static_assert(offsetof(Nx2RawFault,frame)==NX2_RAW_FRAME_OFFSET,"raw frame ABI");
_Static_assert(offsetof(Nx2RawFault,gpr9_29)==NX2_RAW_GPRS_OFFSET,"raw GPR ABI");
_Static_assert(offsetof(Nx2RawFault,entry_lr)==NX2_RAW_ENTRY_LR_OFFSET,"raw LR ABI");
_Static_assert(sizeof(Nx2RawFault)==336,"raw record ABI");
#endif
