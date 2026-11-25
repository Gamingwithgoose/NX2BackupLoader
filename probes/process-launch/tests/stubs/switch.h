/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef uint32_t u32,Result,Handle;
typedef uint64_t u64;
typedef int32_t s32;
typedef int64_t s64;
typedef unsigned LimitableResource;
#define INVALID_HANDLE 0u
#define CUR_PROCESS_HANDLE 0xffff8001u
#define R_FAILED(x) ((x)!=0)
enum {InfoType_CoreMask=0,InfoType_PriorityMask=1,InfoType_AslrRegionAddress=12,InfoType_AslrRegionSize=13};
enum {ProcessInfoType_ProcessState=0,ProcessState_Created=0,ProcessState_Running=2,ProcessState_Crashed=3,ProcessState_Exited=6};
enum {MemType_Unmapped=0,Perm_Rx=5};
typedef struct {u64 addr,size;u32 type;} MemoryInfo;
bool envIsSyscallHinted(unsigned);
Result svcGetInfo(u64 *,u32,Handle,u64);
Result svcGetProcessId(u64 *,Handle);
Result svcCreateResourceLimit(Handle *);
Result svcSetResourceLimitLimitValue(Handle,LimitableResource,u64);
Result svcCreateProcess(Handle *,const void *,const u32 *,u64);
Result svcQueryMemory(MemoryInfo *,u32 *,u64);
Result svcMapProcessMemory(void *,Handle,u64,u64);
Result svcUnmapProcessMemory(void *,Handle,u64,u64);
Result svcSetProcessMemoryPermission(Handle,u64,u64,u32);
Result svcStartProcess(Handle,s32,s32,u32);
Result svcGetProcessInfo(s64 *,Handle,unsigned);
Result svcTerminateProcess(Handle);
Result svcCloseHandle(Handle);
void svcSleepThread(s64);
void armDCacheFlush(void *,size_t);
