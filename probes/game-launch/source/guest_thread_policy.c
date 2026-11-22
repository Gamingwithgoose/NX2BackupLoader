/* SPDX-License-Identifier: GPL-2.0-only */
#include "guest_thread_policy.h"
int nx2_guest_main_stack(uint32_t declared_bytes,uint32_t *allocated_bytes){
 if(!allocated_bytes || declared_bytes>NX2_GUEST_STACK_CAP)return 0;
 uint32_t n=declared_bytes>NX2_GUEST_STACK_BASE
            ?declared_bytes:NX2_GUEST_STACK_BASE;
 n=(n+UINT32_C(0xfff))&~UINT32_C(0xfff);
 if(n>NX2_GUEST_STACK_CAP)return 0;
 *allocated_bytes=n;
 return 1;
}
