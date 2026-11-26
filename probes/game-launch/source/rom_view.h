/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct {
 const unsigned char *bytes,*entries,*strings,*metadata;
 uint64_t metadata_size,total,data_base,strings_size;
 uint32_t count;
} Nx2RomView;
typedef uint32_t (*Nx2RomReadFile)(void *,uint32_t,uint64_t,void *,uint64_t);
int nx2_rom_view(const void *,size_t,Nx2RomView *);
uint32_t nx2_rom_read(const Nx2RomView *,uint64_t,void *,uint64_t,Nx2RomReadFile,void *);
const char *nx2_rom_path(const Nx2RomView *,uint32_t);
uint32_t nx2_rom_source(const Nx2RomView *,uint32_t);
