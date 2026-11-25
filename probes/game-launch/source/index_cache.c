/* SPDX-License-Identifier: GPL-2.0-only */
#include "index_cache.h"
#include "rom_view.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(path,mode) _mkdir(path)
#endif

#define CACHE_LIMIT (128u * 1024u * 1024u)
#ifndef CACHE_DIR
#define CACHE_DIR "sdmc:/switch/BackupLoaderMKW/cache"
#endif
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
static uint64_t identity(const char *root,const char *base){
  uint64_t h=hash_root(root);
  if(!base || !*base)return h;
  h=hash_bytes(h,"\0UNION1\0",8);
  return hash_bytes(h,base,strlen(base));
}
static int filename(const char *root,const char *base,char *out,size_t capacity) {
  int n = snprintf(out, capacity, CACHE_DIR "/%016llx.idx",
                   (unsigned long long)identity(root,base));
  return n > 0 && (size_t)n < capacity;
}
static int64_t modified(const char *root) {
  struct stat st;
  return stat(root, &st) ? -1 : (int64_t)st.st_mtime;
}
static int64_t stamp(const char *root,const char *base){
  int64_t a=modified(root);
  if(a<0 || !base || !*base)return a;
  int64_t b=modified(base);
  if(b<0)return -1;
  uint64_t h=hash_bytes(UINT64_C(14695981039346656037),&a,sizeof(a));
  return (int64_t)(hash_bytes(h,&b,sizeof(b))&INT64_MAX);
}
void *nx2_cache_load(const char *root,size_t *bytes){
  return nx2_cache_load_layered(root,NULL,bytes);
}
void *nx2_cache_load_layered(const char *root,const char *base,size_t *bytes) {
  char path[160];
  CacheHeader header;
  *bytes = 0;
  if (!filename(root,base,path,sizeof(path))) return NULL;
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  void *data = NULL;
  int64_t modified_stamp=stamp(root,base);
  if (modified_stamp < 0 || fread(&header, 1, sizeof(header), f) != sizeof(header) ||
      memcmp(header.magic, "NX2IDX01", 8) || header.root_hash != identity(root,base) ||
      header.root_mtime != modified_stamp || header.size < 80 ||
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
  if (!nx2_rom_view(data, (size_t)header.size, &view) ||
      ((const unsigned char *)data)[8]!=(base&&*base?2:1)) {
    free(data);
    data = NULL;
    goto done;
  }
  *bytes = (size_t)header.size;
done:
  fclose(f);
  return data;
}
int nx2_cache_save(const char *root,const void *data,size_t bytes){
  return nx2_cache_save_layered(root,NULL,data,bytes);
}
int nx2_cache_save_layered(const char *root,const char *base,const void *data,size_t bytes) {
  char path[160], tmp[170];
  if (!data || bytes < 80 || bytes > CACHE_LIMIT ||
      !filename(root,base,path,sizeof(path))) return 0;
  int64_t modified_stamp=stamp(root,base);
  if (modified_stamp < 0) return 0;
  mkdir("sdmc:/switch", 0777);
  mkdir("sdmc:/switch/BackupLoaderMKW", 0777);
  mkdir(CACHE_DIR, 0777);
  CacheHeader header = {{'N','X','2','I','D','X','0','1'}, identity(root,base),
                        (uint64_t)bytes,
                        hash_bytes(UINT64_C(14695981039346656037), data, bytes), modified_stamp};
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
