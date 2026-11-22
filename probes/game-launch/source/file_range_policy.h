/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>

/* Validate actual IFile::OperateRange arguments, not IStorage/ROMFS limits.
 * Only non-destructive operations are exposed to guest save/cache files. */
int nx2_file_range_allowed(uint32_t operation,int64_t offset,int64_t length,
                           const void *out,uint64_t out_size,
                           const void *in,uint64_t in_size);
