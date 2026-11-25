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
/* Virtual guest object capacity is intentionally larger than lightweight SDK needs. */
#define NX2_HOST_HANDLE_LIMIT 512U
typedef struct {
  FsFileSystem *sd;
  FILE *report;
  char rom_root[1200], rom_base_root[1400], save_root[512];
  Nx2HostFile handles[NX2_HOST_HANDLE_LIMIT];
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
  /* Demand-built per-launch loose RomFS cache, max 256 KiB allocated. */
  struct {
    unsigned char *data;
    uint32_t file;
    uint64_t length,stamp;
    uint32_t valid;
  } small_rom[8];
  uint32_t small_seen[64]; /* file index + 1, direct mapped */
  uint64_t small_clock,small_hits,small_fills,small_fill_failures;
  /* Storage worker publishes this before blocking SD operations. */
  uint64_t rpc_started_tick,rpc_completed_count;
  uint32_t rpc_active_slot,rpc_active_op;
  /* Serialized worker breadcrumbs, read by the watchdog only. */
  uint32_t rpc_rom_phase,rpc_rom_file;
  uint64_t rpc_rom_offset,rpc_rom_bytes;
  uint64_t rpc_rom_completed;
  uint32_t rpc_rom_chunk_bytes,rpc_rom_result;
  int abort;
} Nx2Host;
int nx2_host_init(Nx2Host *, FILE *, const char *, const char *,uint64_t);
int nx2_host_poll(Nx2Host *, Nx2Launch *);
void nx2_host_close(Nx2Host *);
int nx2_save_path(char *, size_t, const char *, const char *);
