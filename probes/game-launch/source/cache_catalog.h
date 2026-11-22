/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include <stddef.h>
#define NX2_CACHE_INFO_BYTES 0x60U
#define NX2_CACHE_QUOTA_MAGIC UINT64_C(0x31534E4532484343)
int nx2_cache_parse_index(const char *name,uint16_t *index);
/* Validate a persistent per-title 24-byte capacity marker. */
int nx2_cache_unpack_quota(const void *bytes,size_t length,
                           uint64_t *data_size,uint64_t *journal_size);
int nx2_cache_pack_info(void *output,size_t length,uint64_t guest_id,
                        uint16_t index,uint64_t data_size,uint64_t journal_size);
