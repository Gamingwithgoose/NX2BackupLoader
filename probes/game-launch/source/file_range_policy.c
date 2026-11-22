/* SPDX-License-Identifier: GPL-2.0-only */
#include "file_range_policy.h"
int nx2_file_range_allowed(uint32_t operation,int64_t offset,int64_t length,
                           const void *out,uint64_t out_size,
                           const void *in,uint64_t in_size){
 if(offset<0||length<0||offset>INT64_MAX-length)return 0;
 if(operation==2) /* FsOperationId_InvalidateCache */
   return out_size==0 && in_size==0;
 if(operation==3) /* FsOperationId_QueryRange, 0x40-byte FsRangeInfo */
   return out && out_size==0x40 && !in && in_size==0;
 return 0;
}

int nx2_save_create_request_valid(uint64_t size,uint64_t flags){
 /* The underlying fsFsCreateFile accepts a signed 64-bit file size and
  * its only supported option is the BigFile bit. */
 return size<=INT64_MAX && (flags & ~UINT64_C(1))==0;
}

int nx2_save_write_request_valid(uint64_t offset,uint64_t size,uint64_t options){
 return offset<=INT64_MAX && size<=INT64_MAX-offset &&
        (options & ~UINT64_C(1))==0;
}
int nx2_save_resize_request_valid(uint64_t size){
 return size<=INT64_MAX;
}
