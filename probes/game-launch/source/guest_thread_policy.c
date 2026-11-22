/* SPDX-License-Identifier: GPL-2.0-only */
#include "guest_thread_policy.h"
int nx2_guest_main_stack(uint32_t declared_bytes,uint32_t *allocated_bytes){
 if(!allocated_bytes)return 0;
 uint64_t n=declared_bytes>NX2_GUEST_STACK_BASE
            ?declared_bytes:NX2_GUEST_STACK_BASE;
 n=(n+UINT64_C(0xfff))&~UINT64_C(0xfff);
 if(n>UINT32_MAX)return 0;
 *allocated_bytes=(uint32_t)n;
 return 1;
}
