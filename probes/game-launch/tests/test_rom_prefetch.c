/* SPDX-License-Identifier: GPL-2.0-only */
/* cc -std=c11 -Wall -Wextra -Werror -I../source
 *    test_rom_prefetch.c ../source/rom_prefetch.c -o test_rom_prefetch */
#include "rom_prefetch.h"
#include <stdint.h>
#include <stdio.h>
static int check(int test,const char *label){
 if(!test)fprintf(stderr,"FAIL: %s\n",label);
 return !test;
}
int main(void){
 int bad=0;
 bad+=check(nx2_rom_prefetch_bytes(262144,1000000,0)==0,
            "random first read demand only");
 bad+=check(nx2_rom_prefetch_bytes(262144,1000000,1)==0,
            "one sequential read demand only");
 bad+=check(nx2_rom_prefetch_bytes(262144,1000000,2)==524288,
            "two sequential reads permit one extra 256 KiB");
 bad+=check(nx2_rom_prefetch_bytes(65536,1000000,2)==131072,
            "64 KiB requests never fetch a 1 MiB window");
 bad+=check(nx2_rom_prefetch_bytes(262144,300000,8)==300000,
            "end-of-file bounded to recorded extent");
 bad+=check(nx2_rom_prefetch_bytes(262144,262144,8)==0,
            "no prefetch at exact EOF");
 bad+=check(nx2_rom_prefetch_bytes(524288,1000000,10)==0,
            "large demand-only read bypasses speculative path");
 bad+=check(nx2_rom_prefetch_bytes(UINT64_MAX,UINT64_MAX,10)==0,
            "no overflow for a large request");
 printf("ROMFS read-ahead policy: %s\n",bad?"FAIL":"PASS");
 return !!bad;
}
