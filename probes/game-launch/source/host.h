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
/* Native guest file/directory handles grow with available memory. */
typedef struct {
  FsFileSystem *sd;
  FILE *report;
  char rom_root[1200], save_root[512];
  Nx2HostFile *handles;
  size_t handle_capacity;
  struct {
    FsFile file;
    uint64_t used;
    uint32_t index, open, logged;
  } cache[16];
  uint64_t guest_program_id; /* provenance for guest-only cache metadata */
  uint64_t calls, bytes, save_bytes, lines;
  uint64_t nup_large_reads;
  /* Four independent, bounded 512 KiB windows prevent unrelated asset
   * streams from repeatedly evicting a single global 1 MiB cache. Only
   * the serialized host storage worker reads or modifies these fields. */
  struct {
    unsigned char *data;
    uint64_t offset,length,last_end,stamp;
    uint32_t file,valid,previous,streak;
  } rom_windows[4];
  uint64_t rom_window_clock;
  uint64_t read_ahead_hits,read_ahead_fills;
  /* Storage worker publishes this before blocking SD operations. */
  uint64_t rpc_started_tick,rpc_completed_count;
  uint32_t rpc_active_slot,rpc_active_op;
  int abort;
} Nx2Host;
int nx2_host_init(Nx2Host *, FILE *, const char *, const char *,uint64_t);
int nx2_host_poll(Nx2Host *, Nx2Launch *);
void nx2_host_close(Nx2Host *);
int nx2_save_path(char *, size_t, const char *, const char *);
