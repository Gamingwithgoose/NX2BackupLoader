/* SPDX-License-Identifier: GPL-2.0-only */
/* cc -std=c11 -Wall -Wextra -Werror -I../source
 *    test_guest_thread_policy.c ../source/guest_thread_policy.c
 *    -o test_guest_thread_policy */
#include "guest_thread_policy.h"
#include <stdint.h>
#include <stdio.h>
static int check(int ok,const char *label){
 if(!ok)fprintf(stderr,"FAIL %s\n",label);
 return !ok;
}
int main(void){
 int bad=0;uint32_t n=0;
 bad+=check(nx2_guest_main_stack(0,&n)&&n==0x100000U,
            "unspecified remains 1 MiB");
 bad+=check(nx2_guest_main_stack(0x40000,&n)&&n==0x100000U,
            "small NPDM never shrinks baseline");
 bad+=check(nx2_guest_main_stack(0x100000,&n)&&n==0x100000U,
            "original 1 MiB baseline preserved");
 bad+=check(nx2_guest_main_stack(0x180000,&n)&&n==0x180000U,
            "honor a 1.5 MiB NPDM stack");
 bad+=check(nx2_guest_main_stack(0x200001,&n)&&n==0x201000U,
            "non-aligned stack rounds to 4 KiB");
 bad+=check(nx2_guest_main_stack(0x800000,&n)&&n==0x800000U,
            "honor an 8 MiB NPDM stack");
 bad+=check(nx2_guest_main_stack(0x1000000,&n)&&n==0x1000000U,
            "safe max accepted");
 bad+=check(!nx2_guest_main_stack(0x1000001,&n),
            "larger than cap rejected instead of truncated");
 bad+=check(!nx2_guest_main_stack(UINT32_MAX,&n),
            "uint32 overflow prohibited");
 bad+=check(!nx2_guest_main_stack(0,0),
            "null output rejected");
 puts(bad?"Guest NPDM stack selection: FAIL":"Guest NPDM stack selection: PASS");
 return !!bad;
}
