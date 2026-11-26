/* SPDX-License-Identifier: GPL-2.0-only */
#include "raw_fault.h"
/* Capture BEFORE libounce touches the incoming SP, queries GetInfo, edits
 * the kernel context, or returns from exception to invoke its C handler.
 * The initial snapshot has no stack/calls/TLS/allocator/FSP/SVC access.
 * Version2 can attempt native persistence AFTER capture on a private stack.
 * x0/x1 and x9..x30 are preserved. Scratch x2..x8 are already represented
 * in the kernel's context; libounce obtains those original GPRs there.
 * ABI source: ouncebrew/libounce nx/source/runtime/exception.s/crt0.s.
 * Independent implementation; does not alter HBL's forwarding variable. */
.section .text.nx2_raw_fault_entry,"ax",%progbits
.balign 4
.global __wrap___libnx_exception_entry
.type __wrap___libnx_exception_entry,%function
__wrap___libnx_exception_entry:
    adrp x2, nx2_raw_fault
    add x2, x2, :lo12:nx2_raw_fault
1:
    ldaxr w3, [x2]
    cbnz w3, 8f
    mov w3, #1
    stlxr w4, w3, [x2]
    cbnz w4, 1b
    str x0, [x2, #NX2_RAW_KIND_OFFSET]
    str x1, [x2, #NX2_RAW_CONTEXT_OFFSET]
    mov x3, sp
    str x3, [x2, #NX2_RAW_SP_OFFSET]
    mrs x3, cntpct_el0
    str x3, [x2, #NX2_RAW_TICK_OFFSET]
    stp x9, x10, [x2, #NX2_RAW_GPRS_OFFSET]
    stp x11, x12, [x2, #(NX2_RAW_GPRS_OFFSET+16)]
    stp x13, x14, [x2, #(NX2_RAW_GPRS_OFFSET+32)]
    stp x15, x16, [x2, #(NX2_RAW_GPRS_OFFSET+48)]
    stp x17, x18, [x2, #(NX2_RAW_GPRS_OFFSET+64)]
    stp x19, x20, [x2, #(NX2_RAW_GPRS_OFFSET+80)]
    stp x21, x22, [x2, #(NX2_RAW_GPRS_OFFSET+96)]
    stp x23, x24, [x2, #(NX2_RAW_GPRS_OFFSET+112)]
    stp x25, x26, [x2, #(NX2_RAW_GPRS_OFFSET+128)]
    stp x27, x28, [x2, #(NX2_RAW_GPRS_OFFSET+144)]
    str x29, [x2, #(NX2_RAW_GPRS_OFFSET+160)]
    str x30, [x2, #NX2_RAW_ENTRY_LR_OFFSET]
    /* x1 comes from the kernel exception ABI. Reject obvious invalid
     * pointers; never walk a stack or arbitrary guest pointer chain. */
    cbz x1, 3f
    tst x1, #7
    b.ne 3f
    lsr x3, x1, #48
    cbnz x3, 3f
    adds x3, x1, #120
    b.cs 3f
    ldp x3, x4, [x1, #0]
    stp x3, x4, [x2, #NX2_RAW_FRAME_OFFSET]
    ldp x3, x4, [x1, #16]
    stp x3, x4, [x2, #(NX2_RAW_FRAME_OFFSET+16)]
    ldp x3, x4, [x1, #32]
    stp x3, x4, [x2, #(NX2_RAW_FRAME_OFFSET+32)]
    ldp x3, x4, [x1, #48]
    stp x3, x4, [x2, #(NX2_RAW_FRAME_OFFSET+48)]
    ldp x3, x4, [x1, #64]
    stp x3, x4, [x2, #(NX2_RAW_FRAME_OFFSET+64)]
    ldp x3, x4, [x1, #80]
    stp x3, x4, [x2, #(NX2_RAW_FRAME_OFFSET+80)]
    ldp x3, x4, [x1, #96]
    stp x3, x4, [x2, #(NX2_RAW_FRAME_OFFSET+96)]
    ldr x3, [x1, #112]
    str x3, [x2, #(NX2_RAW_FRAME_OFFSET+112)]
    mov w3, #1
    str w3, [x2, #4]
3:
    mov w3, #2
    stlr w3, [x2]
    sev
    b 5f
.Lraw_grace:
    /* Bounded100ms opportunity for the healthy host loop to persist the
     * first PC/LR/FAR line. No unbounded WFE if the host itself faulted.
     * Afterwards always forward the original exception kind/context. */
    mrs x4, cntpct_el0
    movz x5, #(NX2_RAW_GRACE_TICKS & 0xffff)
    movk x5, #(NX2_RAW_GRACE_TICKS >> 16), lsl #16
4:
    ldar w3, [x2]
    cmp w3, #3
    b.eq 9f
    mrs x6, cntpct_el0
    sub x6, x6, x4
    cmp x6, x5
    b.hs 9f
    yield
    b 4b
5:
    /* Only a captured DataAbort uses the experimental native writer.
     * Other faults retain the RAM observer and original handler path.
     * Avoid SIMD operations for FP/SIMD-access exception classes. */
    ldr w3, [x2, #4]
    cbz w3, .Lraw_grace
    ldr w3, [x2, #(NX2_RAW_FRAME_OFFSET+108)]
    lsr w3, w3, #26
    cmp w3, #0x24
    b.eq 6f
    cmp w3, #0x25
    b.ne .Lraw_grace
6:
    adrp x3, nx2_raw_fault_trace_stack_end
    add x3, x3, :lo12:nx2_raw_fault_trace_stack_end
    mov sp, x3
    sub sp, sp, #528
    stp q0, q1, [sp, #0]
    stp q2, q3, [sp, #32]
    stp q4, q5, [sp, #64]
    stp q6, q7, [sp, #96]
    stp q8, q9, [sp, #128]
    stp q10, q11, [sp, #160]
    stp q12, q13, [sp, #192]
    stp q14, q15, [sp, #224]
    stp q16, q17, [sp, #256]
    stp q18, q19, [sp, #288]
    stp q20, q21, [sp, #320]
    stp q22, q23, [sp, #352]
    stp q24, q25, [sp, #384]
    stp q26, q27, [sp, #416]
    stp q28, q29, [sp, #448]
    stp q30, q31, [sp, #480]
    mrs x3, fpcr
    mrs x4, fpsr
    str x3, [sp, #512]
    str x4, [sp, #520]
    bl nx2_raw_fault_trace_write
    ldr x3, [sp, #512]
    ldr x4, [sp, #520]
    msr fpcr, x3
    msr fpsr, x4
    ldp q0, q1, [sp, #0]
    ldp q2, q3, [sp, #32]
    ldp q4, q5, [sp, #64]
    ldp q6, q7, [sp, #96]
    ldp q8, q9, [sp, #128]
    ldp q10, q11, [sp, #160]
    ldp q12, q13, [sp, #192]
    ldp q14, q15, [sp, #224]
    ldp q16, q17, [sp, #256]
    ldp q18, q19, [sp, #288]
    ldp q20, q21, [sp, #320]
    ldp q22, q23, [sp, #352]
    ldp q24, q25, [sp, #384]
    ldp q26, q27, [sp, #416]
    ldp q28, q29, [sp, #448]
    ldp q30, q31, [sp, #480]
    adrp x2, nx2_raw_fault
    add x2, x2, :lo12:nx2_raw_fault
    ldr x3, [x2, #NX2_RAW_SP_OFFSET]
    mov sp, x3
    ldr x0, [x2, #NX2_RAW_KIND_OFFSET]
    ldr x1, [x2, #NX2_RAW_CONTEXT_OFFSET]
    ldp x9, x10, [x2, #NX2_RAW_GPRS_OFFSET]
    ldp x11, x12, [x2, #(NX2_RAW_GPRS_OFFSET+16)]
    ldp x13, x14, [x2, #(NX2_RAW_GPRS_OFFSET+32)]
    ldp x15, x16, [x2, #(NX2_RAW_GPRS_OFFSET+48)]
    ldp x17, x18, [x2, #(NX2_RAW_GPRS_OFFSET+64)]
    ldp x19, x20, [x2, #(NX2_RAW_GPRS_OFFSET+80)]
    ldp x21, x22, [x2, #(NX2_RAW_GPRS_OFFSET+96)]
    ldp x23, x24, [x2, #(NX2_RAW_GPRS_OFFSET+112)]
    ldp x25, x26, [x2, #(NX2_RAW_GPRS_OFFSET+128)]
    ldp x27, x28, [x2, #(NX2_RAW_GPRS_OFFSET+144)]
    ldr x29, [x2, #(NX2_RAW_GPRS_OFFSET+160)]
    ldr x30, [x2, #NX2_RAW_ENTRY_LR_OFFSET]
    b .Lraw_grace
8:
    clrex
9:
    b __real___libnx_exception_entry
.size __wrap___libnx_exception_entry, .-__wrap___libnx_exception_entry

.section .bss.nx2_raw_fault_trace_stack,"aw",%nobits
.balign 16
nx2_raw_fault_trace_stack:
    .space 0x8000
nx2_raw_fault_trace_stack_end:
