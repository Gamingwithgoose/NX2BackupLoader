/* SPDX-License-Identifier: GPL-2.0-only */
#include "index_cache.h"
#include "rom_view.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CACHE_LIMIT (128u * 1024u * 1024u)
#define CACHE_DIR "sdmc:/switch/BackupLoaderMKW/cache"
typedef struct {
  char magic[8];
  uint64_t root_hash, size, content_hash;
  int64_t root_mtime;
} CacheHeader;

static uint64_t hash_bytes(uint64_t h, const void *data, size_t size) {
  const unsigned char *p = data;
  for (size_t i = 0; i < size; i++) {
    h ^= p[i];
    h *= UINT64_C(1099511628211);
  }
  return h;
}
static uint64_t hash_root(const char *root) {
  return hash_bytes(UINT64_C(14695981039346656037), root, strlen(root));
}
static int filename(const char *root, char *out, size_t capacity) {
  int n = snprintf(out, capacity, CACHE_DIR "/%016llx.idx",
                   (unsigned long long)hash_root(root));
  return n > 0 && (size_t)n < capacity;
}
static int64_t modified(const char *root) {
  struct stat st;
  return stat(root, &st) ? -1 : (int64_t)st.st_mtime;
}
void *nx2_cache_load(const char *root, size_t *bytes) {
  char path[160];
  CacheHeader header;
  *bytes = 0;
  if (!filename(root, path, sizeof(path))) return NULL;
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  void *data = NULL;
  int64_t stamp = modified(root);
  if (stamp < 0 || fread(&header, 1, sizeof(header), f) != sizeof(header) ||
      memcmp(header.magic, "NX2IDX01", 8) || header.root_hash != hash_root(root) ||
      header.root_mtime != stamp || header.size < 80 ||
      header.size > CACHE_LIMIT) goto done;
  data = malloc((size_t)header.size);
  if (!data) goto done;
  if (fread(data, 1, (size_t)header.size, f) != (size_t)header.size ||
      fgetc(f) != EOF ||
      hash_bytes(UINT64_C(14695981039346656037), data, (size_t)header.size) != header.content_hash) {
    free(data);
    data = NULL;
    goto done;
  }
  Nx2RomView view;
  if (!nx2_rom_view(data, (size_t)header.size, &view)) {
    free(data);
    data = NULL;
    goto done;
  }
  *bytes = (size_t)header.size;
done:
  fclose(f);
  return data;
}
int nx2_cache_save(const char *root, const void *data, size_t bytes) {
  char path[160], tmp[170];
  if (!data || bytes < 80 || bytes > CACHE_LIMIT ||
      !filename(root, path, sizeof(path))) return 0;
  int64_t stamp = modified(root);
  if (stamp < 0) return 0;
  mkdir("sdmc:/switch", 0777);
  mkdir("sdmc:/switch/BackupLoaderMKW", 0777);
  mkdir(CACHE_DIR, 0777);
  CacheHeader header = {{'N','X','2','I','D','X','0','1'}, hash_root(root),
                        (uint64_t)bytes,
                        hash_bytes(UINT64_C(14695981039346656037), data, bytes), stamp};
  if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return 0;
  FILE *f = fopen(tmp, "wb");
  if (!f) return 0;
  int ok = fwrite(&header, 1, sizeof(header), f) == sizeof(header) &&
           fwrite(data, 1, bytes, f) == bytes;
  if (fflush(f)) ok = 0;
  if (fclose(f)) ok = 0;
  if (ok) {
    remove(path);
    if (!rename(tmp, path)) return 1;
  }
  remove(tmp);
  return 0;
}
