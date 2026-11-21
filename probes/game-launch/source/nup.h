/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include <stddef.h>
/* NUP v1 base-package reader. Never extracts the entire archive. */
int nx2_nup_probe(const char *path, uint64_t *title_id, uint64_t *files);
int nx2_nup_open(const char *path);
void nx2_nup_close(void);
int nx2_nup_active(void);
int nx2_nup_file_info(const char *path, uint64_t *size, unsigned *codec);
int nx2_nup_read(const char *path, uint64_t offset, void *dest, size_t length);
void *nx2_nup_read_all(const char *path, size_t limit, size_t *length);
typedef int (*Nx2NupVisit)(void *context,const char *path,int is_directory,uint64_t size);
int nx2_nup_romfs_walk(Nx2NupVisit visitor, void *context);
const char *nx2_nup_probe_error(void);

/* Extract NPDM TLV type 0 from selected package metadata, if present. */
void *nx2_nup_package_npdm(size_t *length);

/* Error produced by the last archive entry read or checksum verification. */
const char *nx2_nup_read_error(void);
int nx2_nup_entry_detail(const char *path, uint64_t *stored, uint64_t *offset);
/* Fast NUP raw-entry read-ahead counters: cache hits, speculative SD
 * fills, direct SD reads, and bytes fetched into the read-ahead windows. */
void nx2_nup_raw_stats(uint64_t out[4]);
