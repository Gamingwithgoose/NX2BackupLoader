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
 /* Physical Switch 2 heap-budget regression: a large guest requests
  * 7,866 MiB. Previously a 1-GiB host reservation left only 7,864 MiB
  * of guest address space. The new policy must provide a genuine larger
  * span while leaving actual physical memory headroom. No game IDs used. */
 const uint64_t override_base=UINT64_C(0x40b02d8000);
 const uint64_t override_bytes=UINT64_C(9320955904);
 const uint64_t kernel_heap_base=UINT64_C(0x40ac200000);
 const uint64_t old_retained=UINT64_C(1142947840);
 const uint64_t old_available=UINT64_C(8250032128);
 const uint64_t large_request=UINT64_C(0x1eba00000);
 Nx2HeapPlan plan;
 fails+=check(nx2_heap_plan(override_base,override_bytes,&plan)==NX2_HEAP_READY,
              "large host override remains plan-able");
 fails+=check(plan.host_bytes>=NX2_HOST_MIN_BYTES,
              "host allocator receives at least safe committed prefix");
 fails+=check(plan.game_bytes>=large_request,
              "large guest request fits reserved native heap span");
 const uint64_t retained=plan.game_begin-kernel_heap_base;
 fails+=check(retained<old_retained,
              "release genuine additional host heap pages");
 const uint64_t recovered=old_retained-retained;
 fails+=check(nx2_heap_growth_allowed(0,large_request,old_available+recovered,floor),
              "large request can pass real physical headroom policy");
 fails+=check(!nx2_heap_growth_allowed(0,large_request,
                                     large_request+floor-1,floor),
              "unbacked guest request is still refused");
 fails+=check(nx2_heap_plan(0,override_bytes,&plan)==NX2_HEAP_INVALID,
              "null heap address refused");
 puts(fails?"Heap growth regression: FAIL":"Heap growth regression: PASS");
 return !!fails;
}
