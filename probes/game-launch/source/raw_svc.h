/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
uint32_t nx2_raw_info(uint64_t *,uint32_t,uint32_t,uint64_t);
uint32_t nx2_raw_connect(uint32_t *,const char *);
uint32_t nx2_raw_sync(uint32_t);
uint32_t nx2_raw_buffer(void *,uint64_t,uint32_t);
uint32_t nx2_raw_close(uint32_t);
uint32_t nx2_raw_map_shared(uint32_t,uint64_t,uint64_t,uint32_t);
uint32_t nx2_raw_unmap_shared(uint32_t,uint64_t,uint64_t);
uint32_t nx2_raw_async(uint32_t *,void *,uint64_t,uint32_t);
uint32_t nx2_raw_map_physical(uint64_t,uint64_t);
uint32_t nx2_raw_unmap_physical(uint64_t,uint64_t);

uint32_t nx2_raw_create_thread(uint32_t *,uintptr_t,uintptr_t,uintptr_t,int32_t,int32_t);
uint32_t nx2_raw_start_thread(uint32_t);
uint32_t nx2_raw_wait_sync(int32_t *,const uint32_t *,int32_t,int64_t);
uint32_t nx2_raw_arbitrate_lock(uint32_t,uintptr_t,uint32_t);
uint32_t nx2_raw_arbitrate_unlock(uintptr_t);
uint32_t nx2_raw_wait_key(uintptr_t,uintptr_t,uint32_t,int64_t);
void nx2_raw_signal_key(uintptr_t,int32_t);
uint32_t nx2_raw_set_memory_permission(uintptr_t,uint64_t,uint32_t);
void nx2_raw_exit_thread(void);
void nx2_raw_sleep(int64_t);
void nx2_raw_exit(void);
void nx2_watchdog_worker(void *);
