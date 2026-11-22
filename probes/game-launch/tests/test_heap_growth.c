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
 const uint64_t floor=UINT64_C(0x40000000);
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
 puts(fails?"Heap growth regression: FAIL":"Heap growth regression: PASS");
 return !!fails;
}
