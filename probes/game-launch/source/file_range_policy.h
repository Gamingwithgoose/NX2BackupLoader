/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>

/* Validate actual IFile::OperateRange arguments, not IStorage/ROMFS limits.
 * Only non-destructive operations are exposed to guest save/cache files. */
int nx2_file_range_allowed(uint32_t operation,int64_t offset,int64_t length,
                           const void *out,uint64_t out_size,
                           const void *in,uint64_t in_size);

/* Validate ABI-safe guest SD save-file creation independent of any 1 GiB
 * launcher cap. Actual space, quotas and permissions are enforced by FS. */
int nx2_save_create_request_valid(uint64_t size,uint64_t flags);
