/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef uint32_t u32,Result;
typedef struct {unsigned handle;} FsFile;
typedef struct {unsigned handle;} FsFileSystem;
#define R_FAILED(x) ((x)!=0)
enum {FsOpenMode_Write=2,FsOpenMode_Append=4,FsWriteOption_Flush=1};
FsFileSystem *fsdevGetDeviceFileSystem(const char *);
Result fsFsOpenFile(FsFileSystem *,const char *,u32,FsFile *);
Result fsFsCreateFile(FsFileSystem *,const char *,int64_t,u32);
Result fsFileSetSize(FsFile *,int64_t);
Result fsFileWrite(FsFile *,int64_t,const void *,uint64_t,u32);
void fsFileClose(FsFile *);
uint64_t armGetSystemTick(void);
