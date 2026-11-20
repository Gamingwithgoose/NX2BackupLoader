/* SPDX-License-Identifier: GPL-2.0-only */
/* Keep SVCs behind normal AArch64 function-call boundaries. Kernel scratch
 * registers must not hold live C locals. Out pointers survive on the stack,
 * matching the SDK/libounce wrappers' calling convention. No TLS/libc used. */
.macro SIMPLE name, number
.section .text.\name,"ax",%progbits
.align 2
.global \name
.type \name,%function
\name:
    svc #\number
    ret
.size \name,.-\name
.endm
.macro OUTPUT name, number, width
.section .text.\name,"ax",%progbits
.align 2
.global \name
.type \name,%function
\name:
    str x0,[sp,#-16]!
    svc #\number
    ldr x2,[sp],#16
    cbz x2,1f
    str \width,[x2]
1:  ret
.size \name,.-\name
.endm
OUTPUT nx2_raw_info, 0x29, x1
OUTPUT nx2_raw_connect, 0x1f, w1
OUTPUT nx2_raw_async, 0x23, w1
SIMPLE nx2_raw_sync, 0x21
SIMPLE nx2_raw_buffer, 0x22
SIMPLE nx2_raw_close, 0x16
SIMPLE nx2_raw_map_physical, 0x2c
SIMPLE nx2_raw_unmap_physical, 0x2d

SIMPLE nx2_raw_sleep, 0x0b
SIMPLE nx2_raw_exit, 0x07
