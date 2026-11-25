/* SPDX-License-Identifier: GPL-2.0-only */
/* Desktop-only native archive I/O fixture. Never in production include paths. */
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef uint32_t Result;
typedef int64_t s64;
typedef struct { int unused; } FsFileSystem;
typedef struct { int unused; } FsFile;
enum {FsOpenMode_Read=1,FsReadOption_None=0};
#define R_FAILED(rc) ((rc)!=0)
FsFileSystem *fsdevGetDeviceFileSystem(const char *);
Result fsFsOpenFile(FsFileSystem *,const char *,uint32_t,FsFile *);
void fsFileClose(FsFile *);
Result fsFileGetSize(FsFile *,s64 *);
Result fsFileRead(FsFile *,uint64_t,void *,uint64_t,uint32_t,uint64_t *);
