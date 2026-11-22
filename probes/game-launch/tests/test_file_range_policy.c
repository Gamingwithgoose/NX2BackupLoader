/* SPDX-License-Identifier: GPL-2.0-only */
/* cc -std=c11 -Wall -Wextra -Werror -I../source
 *   test_file_range_policy.c ../source/file_range_policy.c
 *   -o test_file_range_policy */
#include "file_range_policy.h"
#include <stdint.h>
#include <stdio.h>
static int check(int ok,const char *reason){
 if(!ok)fprintf(stderr,"FAIL %s\n",reason);
 return !ok;
}
int main(void){
 int bad=0;unsigned char range[0x40]={0};
 bad+=check(nx2_file_range_allowed(3,0,0,range,64,0,0),"query empty");
 bad+=check(nx2_file_range_allowed(3,4096,8192,range,64,0,0),"query file range");
 bad+=check(nx2_file_range_allowed(2,4096,8192,0,0,0,0),"invalidate file cache");
 bad+=check(!nx2_file_range_allowed(0,0,0,0,0,0,0),"clear is prohibited");
 bad+=check(!nx2_file_range_allowed(1,0,0,0,0,0,0),"clear signature prohibited");
 bad+=check(!nx2_file_range_allowed(3,-1,1024,range,64,0,0),"negative offset");
 bad+=check(!nx2_file_range_allowed(3,0,-1,range,64,0,0),"negative size");
 bad+=check(!nx2_file_range_allowed(3,INT64_MAX,1,range,64,0,0),"offset overflow");
 bad+=check(!nx2_file_range_allowed(3,0,1,0,64,0,0),"missing output");
 bad+=check(!nx2_file_range_allowed(3,0,1,range,63,0,0),"wrong output length");
 bad+=check(!nx2_file_range_allowed(3,0,1,range,64,range,0),"unexpected query input");
 bad+=check(!nx2_file_range_allowed(2,0,1,range,64,0,0),"invalidate unexpected output");
 bad+=check(!nx2_file_range_allowed(2,0,1,0,0,range,1),"invalidate unexpected input");
 bad+=check(nx2_save_create_request_valid(0,0),"zero-length file");
 bad+=check(nx2_save_create_request_valid(UINT64_C(0x40000001),0),
            "file larger than old 1 GiB launcher cap");
 bad+=check(nx2_save_create_request_valid(UINT64_C(0x200000000),1),
            "8 GiB BigFile forwarded to real filesystem");
 bad+=check(nx2_save_create_request_valid((uint64_t)INT64_MAX,1),
            "signed 64-bit API maximum");
 bad+=check(!nx2_save_create_request_valid((uint64_t)INT64_MAX+1,1),
            "reject size exceeding signed API");
 bad+=check(!nx2_save_create_request_valid(512,2),"reject unsupported flag");
 bad+=check(!nx2_save_create_request_valid(512,UINT64_MAX),
            "reject invalid flag bitfield");
 bad+=check(nx2_save_write_request_valid(UINT64_C(0x40000001),4096,0),
            "write above previous 1 GiB cap");
 bad+=check(nx2_save_write_request_valid(UINT64_C(0x200000000),65536,1),
            "8 GiB write with Flush option");
 bad+=check(nx2_save_write_request_valid((uint64_t)INT64_MAX-1,1,0),
            "largest signed write endpoint");
 bad+=check(nx2_save_write_request_valid((uint64_t)INT64_MAX,0,0),
            "zero-length write at max offset");
 bad+=check(!nx2_save_write_request_valid((uint64_t)INT64_MAX,1,0),
            "write endpoint overflow");
 bad+=check(!nx2_save_write_request_valid((uint64_t)INT64_MAX+1,0,0),
            "write offset beyond signed ABI");
 bad+=check(!nx2_save_write_request_valid(16,10,2),
            "reject invalid write option");
 bad+=check(nx2_save_resize_request_valid(UINT64_C(0x200000000)),
            "8 GiB file resize allowed by loader");
 bad+=check(nx2_save_resize_request_valid((uint64_t)INT64_MAX),
            "max signed file resize");
 bad+=check(!nx2_save_resize_request_valid((uint64_t)INT64_MAX+1),
            "reject resize beyond signed ABI");
 puts(bad?"Guest IFile large-file policy: FAIL":
          "Guest IFile large-file policy: PASS");
 return !!bad;
}
