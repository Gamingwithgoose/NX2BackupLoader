/* SPDX-License-Identifier: GPL-2.0-only */
.text
.align 2
.global nx2_enter_guest_stack
.type nx2_enter_guest_stack, %function
nx2_enter_guest_stack:
    /* One-way handoff on the ORIGINAL kernel main thread. The private
     * stack has the selected NPDM's size. No host frames survive on it. */
    and x1, x1, #-16
    mov sp, x1
    mov x29, xzr
    mov x30, xzr
    b nx2_game_worker
.size nx2_enter_guest_stack, .-nx2_enter_guest_stack
