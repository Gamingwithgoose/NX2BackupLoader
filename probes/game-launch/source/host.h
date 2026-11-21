/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "launch.h"
#include <stdio.h>
#include <switch.h>
typedef struct {
  uint32_t kind, mode, generation;
  FsFile file;
  FsDir dir;
} Nx2HostFile;
typedef struct {
  FsFileSystem *sd;
  FILE *report;
  char rom_root[1200], save_root[512];
  Nx2HostFile handles[128];
  struct {
    FsFile file;
    uint64_t used;
    uint32_t index, open, logged;
  } cache[16];
  uint64_t calls, bytes, save_bytes, lines;
  uint64_t nup_large_reads;
  int abort;
} Nx2Host;
int nx2_host_init(Nx2Host *, FILE *, const char *, const char *);
int nx2_host_poll(Nx2Host *, Nx2Launch *);
void nx2_host_close(Nx2Host *);
int nx2_save_path(char *, size_t, const char *, const char *);
