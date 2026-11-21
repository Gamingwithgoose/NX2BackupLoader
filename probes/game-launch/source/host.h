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
  /* 1 MiB read-ahead for shared loose-file and raw-NUP asset reads.
   * This state is only accessed by the serialized storage worker. */
  unsigned char *read_ahead_data;
  uint64_t read_ahead_offset,read_ahead_length,read_ahead_last_end;
  uint64_t read_ahead_hits,read_ahead_fills;
  uint32_t read_ahead_file,read_ahead_last_file;
  uint32_t read_ahead_valid,read_ahead_previous;
  /* Storage worker publishes this before blocking SD operations. */
  uint64_t rpc_started_tick,rpc_completed_count;
  uint32_t rpc_active_slot,rpc_active_op;
  int abort;
} Nx2Host;
int nx2_host_init(Nx2Host *, FILE *, const char *, const char *,uint64_t);
int nx2_host_poll(Nx2Host *, Nx2Launch *);
void nx2_host_close(Nx2Host *);
int nx2_save_path(char *, size_t, const char *, const char *);
