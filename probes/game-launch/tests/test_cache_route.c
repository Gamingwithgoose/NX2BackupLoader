/* SPDX-License-Identifier: GPL-2.0-only */
/* cc -std=c11 -Wall -Wextra -Werror -I../source
 *  test_cache_route.c ../source/cache_route.c -o test_cache_route */
#include "cache_route.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static int check(int ok,const char *label){
 if(!ok)fprintf(stderr,"FAIL %s\n",label);
 return !ok;
}
static void put64(unsigned char *p,uint64_t v){
 for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(v>>(8*i));
}
int main(void){
 const uint64_t minecraft=UINT64_C(0x04008a90274ec000);
 const uint64_t zelda=UINT64_C(0x0400486024dec000);
 unsigned char a[0x40]={0};char dest[64]={0};int bad=0;
 a[0x20]=5;
 bad+=check(nx2_guest_cache_path(minecraft,1,a,sizeof(a),dest,sizeof(dest)) &&
            !strcmp(dest,"cache-storage/0"),"current guest cache zero");
 a[0x22]=2;
 bad+=check(nx2_guest_cache_path(minecraft,1,a,sizeof(a),dest,sizeof(dest)) &&
            !strcmp(dest,"cache-storage/2"),"Minecraft cache index 2");
 a[0x22]=255;a[0x23]=255;
 bad+=check(nx2_guest_cache_path(minecraft,1,a,sizeof(a),dest,sizeof(dest)) &&
            !strcmp(dest,"cache-storage/65535"),"maximum index");
 bad+=check(!nx2_guest_cache_path(minecraft,0,a,sizeof(a),dest,sizeof(dest)),
            "system space refused");
 bad+=check(!nx2_guest_cache_path(minecraft,1,a,12,dest,sizeof(dest)),
            "truncated attribute refused");
 bad+=check(!nx2_guest_cache_path(minecraft,1,a,sizeof(a),dest,16),
            "short destination refused");
 a[0x20]=1;
 bad+=check(!nx2_guest_cache_path(minecraft,1,a,sizeof(a),dest,sizeof(dest)),
            "account save cannot be mapped as a cache");
 a[0x20]=5;put64(a,zelda);
 bad+=check(!nx2_guest_cache_path(minecraft,1,a,sizeof(a),dest,sizeof(dest)),
            "foreign game cache cannot alias Minecraft storage");
 put64(a,minecraft);
 bad+=check(nx2_guest_cache_path(minecraft,1,a,sizeof(a),dest,sizeof(dest)) &&
            !strcmp(dest,"cache-storage/65535"),"explicit own title accepted");
 puts(bad?"Guest cache route tests: FAIL":"Guest cache route tests: PASS");
 return !!bad;
}
