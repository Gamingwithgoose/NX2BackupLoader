/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
/* Shared discovery/mount policy. Limits derive from table storage and the
 * format's 32-bit child IDs, rather than an arbitrary game file count. */
static inline int nx2_nup_layout(uint64_t bytes,uint32_t packages,uint64_t dirs,
 uint64_t files,uint64_t pool,uint64_t names,uint64_t *file_at,uint64_t *children_at){
 if(bytes>128U*1024*1024||!packages||packages>4096)return 0;
 uint64_t start=48+64ULL*packages;
 if(start>bytes||!dirs||dirs>UINT32_MAX||files>UINT32_MAX-dirs||dirs>(bytes-start)/24)return 0;
 uint64_t f=start+24*dirs;
 if(files>(bytes-f)/80)return 0;
 uint64_t c=f+80*files;
 if(pool<c||pool>bytes||names>bytes-pool||(pool-c)%4)return 0;
 if(file_at)*file_at=f;if(children_at)*children_at=c;return 1;
}
