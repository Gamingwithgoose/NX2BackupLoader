/* SPDX-License-Identifier: GPL-2.0-only */
/* cc -std=c11 -Wall -Wextra -Werror -I../source
 *    test_cache_catalog.c ../source/cache_catalog.c -o test_cache_catalog */
#include "cache_catalog.h"
#include <stdio.h>
#include <string.h>
static int check(int c,const char*n){if(!c)fprintf(stderr,"FAILED %s\n",n);return !c;}
static uint64_t get64(const unsigned char*p){uint64_t x=0;for(int i=0;i<8;i++)x|=(uint64_t)p[i]<<(8*i);return x;}
int main(void){
 int failures=0;uint16_t index=0;unsigned char p[0x80];memset(p,0xa5,sizeof(p));
 failures+=check(nx2_cache_parse_index("0",&index)&&index==0,"index zero");
 failures+=check(nx2_cache_parse_index("65535",&index)&&index==65535,"index maximum");
 failures+=check(!nx2_cache_parse_index("65536",&index),"too large");
 failures+=check(!nx2_cache_parse_index("01",&index),"leading zero");
 failures+=check(!nx2_cache_parse_index("2/../3",&index),"invalid path");
 failures+=check(!nx2_cache_parse_index("",&index),"empty index");
 uint64_t guest=UINT64_C(0x04008a90274ec000);
 failures+=check(nx2_cache_pack_info(p,sizeof(p),guest,2,268435456,33554432),"pack correct");
 failures+=check(get64(p)!=0&&p[8]==1&&p[9]==5,"savedata id and types");
 failures+=check(get64(p+0x28)==guest&&get64(p+0x30)==301989888ULL,"guest namespace and quota");
 failures+=check(p[0x38]==2&&p[0x39]==0&&p[0x60]==0xa5,"index and bounds");
 failures+=check(!nx2_cache_pack_info(p,0x5f,guest,2,1,2),"short output buffer");
 failures+=check(!nx2_cache_pack_info(p,0x60,guest,2,UINT64_MAX,1),"size overflow");
 printf("Native cache catalog fixtures: %s\n",failures?"FAILED":"PASS");return failures?1:0;
}
