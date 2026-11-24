/* SPDX-License-Identifier: GPL-2.0-only */
#include "cache_route.h"
#include <string.h>
static uint64_t read_le64(const unsigned char *p){
 uint64_t v=0;
 for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(8*i);
 return v;
}
int nx2_guest_cache_path(uint64_t guest_id,uint8_t space,
                         const unsigned char *attr,size_t attr_len,
                         char *out,size_t capacity){
 /* Cache storage is SaveDataType_Cache=5, SaveDataSpaceId_User=1.
  * attr+0: ApplicationId (zero means current), attr+0x22: index. */
 if(!attr||!out||attr_len<0x40||capacity<20||!guest_id||
    space!=1||attr[0x20]!=5)return 0;
 uint64_t id=read_le64(attr);
 if(id && id!=guest_id)return 0;
 uint32_t index=(uint32_t)attr[0x22]|((uint32_t)attr[0x23]<<8);
 char digits[5];unsigned n=0;
 do{digits[n++]=(char)('0'+index%10U);index/=10U;}while(index);
 if(14U+n>=capacity)return 0;
 memcpy(out,"cache-storage/",14);
 for(unsigned i=0;i<n;i++)out[14+i]=digits[n-1U-i];
 out[14+n]=0;
 return 1;
}
