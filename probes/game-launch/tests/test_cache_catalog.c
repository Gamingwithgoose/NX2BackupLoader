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
 /* First-mount, install-style cache defaults must fit the same durable
  * quota marker protocol as explicit CreateCacheStorage. */
 failures+=check(NX2_CACHE_NOT_FOUND_RESULT==0x202U,"missing-path result");
 failures+=check(NX2_CACHE_INSTALL_DATA_BYTES>0 &&
                 NX2_CACHE_INSTALL_JOURNAL_BYTES>0 &&
                 NX2_CACHE_INSTALL_DATA_BYTES<=INT64_MAX-
                     NX2_CACHE_INSTALL_JOURNAL_BYTES,
                 "bounded auto-provision logical quota");
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
 /* Command 34 reads the real on-disk three-u64 capacity marker.
  * Reject truncated, foreign, corrupt and overflowing metadata. */
 unsigned char marker[24]={0};uint64_t data=0,journal=0;
 uint64_t values[3]={NX2_CACHE_QUOTA_MAGIC,201326592U,67108864U};
 for(unsigned k=0;k<3;k++)
  for(unsigned j=0;j<8;j++)
   marker[8*k+j]=(unsigned char)(values[k]>>(j*8));
 failures+=check(nx2_cache_unpack_quota(marker,sizeof(marker),&data,&journal)&&
                 data==201326592U&&journal==67108864U,"cache quota read");
 failures+=check(!nx2_cache_unpack_quota(marker,23,&data,&journal),
                 "truncated quota");
 uint64_t install_values[3]={NX2_CACHE_QUOTA_MAGIC,
                              NX2_CACHE_INSTALL_DATA_BYTES,
                              NX2_CACHE_INSTALL_JOURNAL_BYTES};
 for(unsigned k=0;k<3;k++)
  for(unsigned j=0;j<8;j++)
   marker[8*k+j]=(unsigned char)(install_values[k]>>(j*8));
 failures+=check(nx2_cache_unpack_quota(marker,sizeof(marker),&data,&journal)&&
                 data==NX2_CACHE_INSTALL_DATA_BYTES &&
                 journal==NX2_CACHE_INSTALL_JOURNAL_BYTES,
                 "install-style first-mount cache marker");
 for(unsigned k=0;k<3;k++)
  for(unsigned j=0;j<8;j++)
   marker[8*k+j]=(unsigned char)(values[k]>>(j*8));
 marker[0]^=1;
 failures+=check(!nx2_cache_unpack_quota(marker,sizeof(marker),&data,&journal),
                 "foreign or corrupt marker");
 marker[0]^=1;
 for(unsigned j=0;j<8;j++)marker[8+j]=0xff;
 failures+=check(!nx2_cache_unpack_quota(marker,sizeof(marker),&data,&journal),
                 "invalid data size");
 for(unsigned j=0;j<8;j++)marker[8+j]=(unsigned char)(INT64_MAX>>(j*8));
 failures+=check(!nx2_cache_unpack_quota(marker,sizeof(marker),&data,&journal),
                 "data and journal total overflow");
 printf("Native cache catalog fixtures: %s\n",failures?"FAILED":"PASS");return failures?1:0;
}
