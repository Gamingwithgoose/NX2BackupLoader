/* SPDX-License-Identifier: GPL-2.0-only */
/* gcc -std=c11 -Wall -Wextra -Werror -I../source
 *     test_heap_growth.c ../source/heap_plan.c -o test_heap_growth */
#include "heap_plan.h"
#include <stdio.h>
static int check(int ok,const char *label){
 if(!ok)fprintf(stderr,"FAIL: %s\n",label);
 return !ok;
}
int main(void){
 const uint64_t floor=NX2_NATIVE_PHYSICAL_FLOOR_BYTES;
 int fails=0;
 fails+=check(nx2_heap_growth_allowed(0x40000000,0x20000000,0,floor),
              "shrinking succeeds even under floor");
 fails+=check(nx2_heap_growth_allowed(0x40000000,0x40000000,0,floor),
              "unchanged heap never consumes headroom");
 fails+=check(nx2_heap_growth_allowed(0,0x10000000,floor+0x10000000,floor),
              "growth exactly meeting physical floor");
 fails+=check(!nx2_heap_growth_allowed(0,0x20000000,floor+0x10000000,floor),
              "growth crossing floor refused");
 fails+=check(!nx2_heap_growth_allowed(0,0x10000000,0x8000000,floor),
              "growth exceeding available refused");
 fails+=check(nx2_heap_growth_allowed(UINT64_MAX,0,0,floor),
              "shrinking avoids overflow");
 /* Current 1-GiB host policy: a fixed 7,866-MiB request cannot fit the
  * 7,864-MiB guest span in this captured override. Do not mask this
  * limitation by changing the allocator policy just to satisfy a test. */
 const uint64_t override_base=UINT64_C(0x40b02d8000);
 const uint64_t override_bytes=UINT64_C(9320955904);
 const uint64_t large_request=UINT64_C(0x1eba00000);
 Nx2HeapPlan plan;
 fails+=check(nx2_heap_plan(override_base,override_bytes,&plan)==NX2_HEAP_READY,
              "large host override remains plan-able");
 fails+=check(plan.host_bytes>=NX2_HOST_MIN_BYTES,
              "host allocator receives at least safe committed prefix");
 fails+=check(plan.game_bytes<large_request,
              "fixed large request exceeds current safe guest span");
 fails+=check(NX2_HOST_MIN_BYTES==UINT64_C(0x40000000) &&
              plan.host_bytes>=NX2_HOST_MIN_BYTES &&
              plan.host_bytes<NX2_HOST_MIN_BYTES+NX2_GAME_ALIGNMENT,
              "1 GiB host prefix plus only alignment overhead");
 fails+=check(plan.game_bytes==UINT64_C(0x1eb800000),
              "captured override retains actual 7864 MiB guest span");
 fails+=check(plan.host_bytes+plan.game_bytes<=override_bytes &&
              plan.game_begin==override_base+plan.host_bytes,
              "host and guest ranges do not overlap or exceed override");
 fails+=check(!nx2_heap_growth_allowed(0,large_request,
                                     large_request+floor-1,floor),
              "unbacked guest request is still refused");
 Nx2HeapPlan invalid_plan;
 fails+=check(nx2_heap_plan(0,override_bytes,&invalid_plan)==NX2_HEAP_INVALID,
              "null heap address refused");
 /* Fast Fusion originally requested the advertised free non-system memory
  * rounded down. When host-reserved pages shrank by 512 MiB, the request
  * grew by exactly 512 MiB, remaining 2 MiB above the guest heap span.
  * A truthful guest budget must cap both virtual and physical availability. */
 uint64_t reported_total=0,reported_used=0;
 const uint64_t non_total=UINT64_C(0x230000000);
 const uint64_t non_used=UINT64_C(0x24428000);
 const uint64_t new_free=UINT64_C(8786903040);
 fails+=check(nx2_guest_memory_budget(non_total,non_used,new_free,
                 UINT64_C(538083328),UINT64_C(8782872576),0,floor,
                 &reported_total,&reported_used),
              "real guest budget returned");
 fails+=check(reported_used==UINT64_C(0x4300000),
              "guest non-system used excludes only host prefix");
 fails+=check(reported_total-reported_used==new_free-floor,
              "guest free budget retains real graphics headroom");
 fails+=check(reported_total-reported_used<UINT64_C(0x20ba00000),
              "Fast Fusion no longer sees unavailable heap as free");
 fails+=check(nx2_guest_memory_budget(non_total,non_used,new_free,
                 UINT64_C(538083328),UINT64_C(0x180000000),0,floor,
                 &reported_total,&reported_used) &&
              reported_total-reported_used==UINT64_C(0x180000000),
              "smaller guest address span caps visible free");
 fails+=check(nx2_guest_memory_budget(non_total,non_used,new_free,
                 UINT64_C(538083328),UINT64_C(0x180000000),
                 UINT64_C(0x20000000),floor,
                 &reported_total,&reported_used) &&
              reported_total-reported_used==UINT64_C(0x160000000),
              "repeated heap requests report only remaining guest span");
 fails+=check(nx2_guest_memory_budget(non_total,non_used,100,floor,
                 UINT64_C(0x4000000),0,floor,
                 &reported_total,&reported_used) &&
              reported_total==reported_used,
              "low physical memory never invents free pages");
 fails+=check(!nx2_guest_memory_budget(1,2,0,0,0,0,floor,
                 &reported_total,&reported_used),
              "invalid kernel accounting refused");
 /* The current host must not reintroduce Fast Fusion's pre-accounting
  * 2 MiB overrequest. Both VM span and real physical headroom cap the
  * reported free pages, even when the host retains an extra 512 MiB. */
 const uint64_t physical_current=new_free-UINT64_C(0x20000000);
 fails+=check(nx2_guest_memory_budget(non_total,non_used,physical_current,
                 plan.host_bytes,plan.game_bytes,0,floor,
                 &reported_total,&reported_used) &&
              reported_total>=reported_used &&
              reported_total-reported_used<=plan.game_bytes &&
              reported_total-reported_used<=physical_current-floor &&
              reported_total-reported_used<large_request,
              "current host advertises only real allocatable guest heap");
 puts(fails?"Heap growth regression: FAIL":"Heap growth regression: PASS");
 return !!fails;
}
