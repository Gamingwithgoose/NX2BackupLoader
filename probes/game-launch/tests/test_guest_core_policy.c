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
 bad+=check(nx2_watchdog_select_core(0x3f,0,0,5)==4,
            "Yoshi: guest main0 storage5 watchdog4");
 bad+=check(nx2_watchdog_select_core(0x3f,0,5,4)==3,
            "guest and storage never share the new watchdog");
 bad+=check(nx2_watchdog_select_core(1,0,0,-1)==-1,
            "no spare preserves original scheduling");
 bad+=check(nx2_watchdog_select_core(7,0,1,2)==-1,
            "three busy cores cannot be separated");
 bad+=check(nx2_watchdog_select_core(0x3f,4,0,5)==3,
            "current host CPU excluded");
 bad+=check(nx2_watchdog_select_core(0,0,0,5)==-1,
            "no available allowed CPU");
 bad+=check(nx2_guest_select_priority(UINT64_MAX,20,44)==20,
            "NPDM main priority honored");
 bad+=check(nx2_guest_select_priority(UINT64_MAX,-1,44)==44,
            "missing NPDM priority preserves legacy");
 bad+=check(nx2_guest_select_priority(UINT64_C(1)<<44,17,44)==44,
            "disallowed NPDM falls back to permitted legacy");
 bad+=check(nx2_guest_select_priority((UINT64_C(1)<<18)|
               (UINT64_C(1)<<50),17,44)==18,
            "closest actual permitted priority");
 bad+=check(nx2_guest_select_priority((UINT64_C(1)<<40)|
               (UINT64_C(1)<<48),44,20)==40,
            "deterministic tie");
 bad+=check(nx2_guest_select_priority(0,44,44)==-1,
            "empty mask rejected");
 bad+=check(nx2_guest_select_priority(UINT64_MAX,255,44)==44,
            "invalid NPDM fallback");
 bad+=check(nx2_storage_select_priority(UINT64_MAX,44)==43,
            "legacy storage 43 retained");
 bad+=check(nx2_storage_select_priority(UINT64_C(1)<<44,44)==44,
            "storage co-schedules when no higher permission");
 bad+=check(nx2_storage_select_priority((UINT64_C(1)<<44)|
              (UINT64_C(1)<<45),44)==44,
            "storage not demoted unnecessarily");
 bad+=check(nx2_storage_select_priority(UINT64_C(1)<<43,44)==-1,
            "storage rejects guest outside mask");
 bad+=check(nx2_storage_select_priority(UINT64_MAX,-1)==-1,
            "storage rejects invalid guest");
 puts(bad?"Guest core/priority policy: FAIL":"Guest core/priority policy: PASS");
 return !!bad;
}
