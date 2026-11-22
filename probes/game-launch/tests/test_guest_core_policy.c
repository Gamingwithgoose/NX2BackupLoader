/* SPDX-License-Identifier: GPL-2.0-only */
/* cc -std=c11 -Wall -Wextra -Werror -I../source
 *    test_guest_core_policy.c ../source/guest_core_policy.c
 *    -o test_guest_core_policy */
#include "guest_core_policy.h"
#include <stdint.h>
#include <stdio.h>
static int check(int ok,const char *label){
 if(!ok)fprintf(stderr,"FAIL %s\n",label);
 return !ok;
}
int main(void){
 int bad=0;
 bad+=check(nx2_guest_select_core(0x3f,0,0)==0,"NPDM core0 beats old core1 default");
 bad+=check(nx2_guest_select_core(0x3f,0,1)==1,"NPDM core1 selected");
 bad+=check(nx2_guest_select_core(0x3f,0,5)==5,"NPDM core5 selected");
 bad+=check(nx2_guest_select_core(0x3f,0,-1)==1,"missing metadata uses old fallback");
 bad+=check(nx2_guest_select_core(0x3f,0,7)==1,"invalid NPDM core uses fallback");
 bad+=check(nx2_guest_select_core(0x3e,0,0)==1,"disallowed core uses fallback");
 bad+=check(nx2_guest_select_core(0x02,1,0)==1,"only host core permitted");
 bad+=check(nx2_guest_select_core(0x01,0,-1)==0,"single allowed core permits co-schedule");
 bad+=check(nx2_guest_select_core(0,0,0)==-1,"no allowed core refused");
 bad+=check(nx2_guest_select_core(UINT64_MAX,0,255)==1,"out of range ignored");
 puts(bad?"Guest core policy: FAIL":"Guest core policy: PASS");
 return !!bad;
}
