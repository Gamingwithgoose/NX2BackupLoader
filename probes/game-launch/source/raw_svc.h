/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
uint32_t nx2_raw_info(uint64_t *,uint32_t,uint32_t,uint64_t);
uint32_t nx2_raw_connect(uint32_t *,const char *);
uint32_t nx2_raw_sync(uint32_t);
uint32_t nx2_raw_buffer(void *,uint64_t,uint32_t);
uint32_t nx2_raw_close(uint32_t);
uint32_t nx2_raw_async(uint32_t *,void *,uint64_t,uint32_t);
uint32_t nx2_raw_map_physical(uint64_t,uint64_t);
uint32_t nx2_raw_unmap_physical(uint64_t,uint64_t);

void nx2_raw_sleep(int64_t);
void nx2_raw_exit(void);
void nx2_watchdog_worker(void *);
