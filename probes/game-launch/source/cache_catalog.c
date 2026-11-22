/* SPDX-License-Identifier: GPL-2.0-only */
#include "cache_catalog.h"
#include <string.h>
static void put64(unsigned char *p,uint64_t x){for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(x>>(i*8));}
static void put16(unsigned char *p,uint16_t x){p[0]=(unsigned char)x;p[1]=(unsigned char)(x>>8);}
int nx2_cache_parse_index(const char *name,uint16_t *index){
 if(!name||!index||!*name)return 0;
 uint32_t n=0;unsigned k=0;
 if(name[0]=='0' && name[1])return 0;
 for(;name[k];k++){
  if(k>=5||name[k]<'0'||name[k]>'9')return 0;
  n=n*10+(unsigned)(name[k]-'0');
  if(n>65535)return 0;
 }
 *index=(uint16_t)n;return 1;
}
static uint64_t get64(const unsigned char *p){
 uint64_t v=0;
 for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(i*8);
 return v;
}
int nx2_cache_unpack_quota(const void *bytes,size_t length,
                           uint64_t *data_size,uint64_t *journal_size){
 if(!bytes||length!=24||!data_size||!journal_size)return 0;
 const unsigned char *p=bytes;
 uint64_t magic=get64(p),data=get64(p+8),journal=get64(p+16);
 if(magic!=NX2_CACHE_QUOTA_MAGIC || data>INT64_MAX ||
    journal>INT64_MAX || data>INT64_MAX-journal)return 0;
 *data_size=data;
 *journal_size=journal;
 return 1;
}
int nx2_cache_pack_info(void *output,size_t length,uint64_t guest_id,
                        uint16_t index,uint64_t data_size,uint64_t journal_size){
 if(!output||length<NX2_CACHE_INFO_BYTES||!guest_id||
    data_size>UINT64_MAX-journal_size)return 0;
 unsigned char *p=output;
 memset(p,0,NX2_CACHE_INFO_BYTES);
 /* This ID refers exclusively to a per-title, SD-backed virtual cache.
  * It is deliberately disjoint from the native host's save IDs. */
 uint64_t id=UINT64_C(0xe000000000000000)|
    ((guest_id^(guest_id>>32))&UINT64_C(0x0000ffffffff0000))|
    (uint64_t)index;
 put64(p,id);
 p[8]=1; /* FsSaveDataSpaceId_User */
 p[9]=5; /* FsSaveDataType_Cache */
 put64(p+0x28,guest_id);
 put64(p+0x30,data_size+journal_size);
 put16(p+0x38,index);
 return 1;
}
