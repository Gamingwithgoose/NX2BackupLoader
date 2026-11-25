/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
typedef uint32_t Result,u32;
typedef struct {uint64_t addr,size;u32 type,perm;} MemoryInfo;
#define INVALID_HANDLE 0u
#define R_FAILED(x) ((x)!=0)
enum {InfoType_DebuggerAttached=8,MemType_CodeStatic=3,Perm_R=1,Perm_Rx=5};
Result svcGetInfo(uint64_t *,u32,u32,uint64_t);
Result svcQueryMemory(MemoryInfo *,u32 *,uint64_t);
