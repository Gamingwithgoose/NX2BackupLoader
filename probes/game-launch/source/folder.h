/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct {
  char name[256], root[1200];
  uint64_t id;
  uint32_t resource;
  uint32_t main_stack_size; /* NPDM META+0x1c, raw bytes */
  uint8_t main_priority; /* NPDM META+0x0e */
  uint8_t main_core; /* NPDM META+0x0f */
  uint8_t has_thread_metadata;
  int updates; /* 2=resolved complete update root, 1=unmerged update present */
  uint64_t update_version;
  int is_nup;
  char nup_status[96];
} Nx2Game;
typedef struct {
  Nx2Game *items;
  size_t count;
} Nx2Catalog;
int nx2_catalog(const char *, Nx2Catalog *);
void nx2_catalog_free(Nx2Catalog *);
int nx2_folder_modules(const char *, char names[13][16], unsigned *);
void *nx2_folder_rom(const char *, size_t *, char error[160]);

void nx2_folder_progress(void (*)(uint32_t, void *), void *);

/* Checked during directory scanning so B can cancel without closing the app. */
void nx2_folder_cancel(int (*fn)(void *), void *ctx);

/* Discard an interrupted scan when the user requests a fresh rebuild. */
void nx2_folder_part_clear(const char *root);

/* Number of files recovered from a completed .part checkpoint. */
uint32_t nx2_folder_resumed_files(void);
