/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include <stddef.h>
#define NX2_CACHE_INFO_BYTES 0x60U
#define NX2_CACHE_QUOTA_MAGIC UINT64_C(0x31534E4532484343)
/* NUPs currently have no guest NACP cache quota metadata. A real installed
 * title may already have its user cache provisioned by Horizon; on the
 * first guest cache mount only, create a private logical cache if missing.
 * These are conservative fallback quotas, not guessed title metadata.
 * No space is preallocated; real SD free-space/errors remain authoritative. */
#define NX2_CACHE_NOT_FOUND_RESULT UINT32_C(0x202)
#define NX2_CACHE_INSTALL_DATA_BYTES UINT64_C(0x08000000) /* 128 MiB */
#define NX2_CACHE_INSTALL_JOURNAL_BYTES UINT64_C(0x02000000) /* 32 MiB */
int nx2_cache_parse_index(const char *name,uint16_t *index);
/* Validate a persistent per-title 24-byte capacity marker. */
int nx2_cache_unpack_quota(const void *bytes,size_t length,
                           uint64_t *data_size,uint64_t *journal_size);
int nx2_cache_pack_info(void *output,size_t length,uint64_t guest_id,
                        uint16_t index,uint64_t data_size,uint64_t journal_size);
