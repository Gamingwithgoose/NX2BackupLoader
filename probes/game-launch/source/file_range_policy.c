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
