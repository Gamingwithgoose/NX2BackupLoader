/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef uint32_t Result;
typedef uint32_t Handle;
typedef unsigned LimitableResource;
#define CUR_PROCESS_HANDLE 0xffff8001U
#define INVALID_HANDLE 0U
enum {InfoType_ResourceLimit=9,InfoType_SystemResourceSizeTotal=16,
      InfoType_SystemResourceSizeUsed=17,InfoType_FreeThreadCount=24};
uint64_t armGetSystemTick(void);
bool envIsSyscallHinted(unsigned);
Result svcGetInfo(uint64_t *,uint32_t,Handle,uint64_t);
Result svcGetResourceLimitLimitValue(int64_t *,Handle,LimitableResource);
Result svcGetResourceLimitCurrentValue(int64_t *,Handle,LimitableResource);
Result svcGetResourceLimitPeakValue(int64_t *,Handle,LimitableResource);
Result svcCloseHandle(Handle);
