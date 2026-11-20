/* SPDX-License-Identifier: GPL-2.0-only */
#include "host.h"
#include "path_guard.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
_Static_assert(sizeof(FsDirectoryEntry) == 0x310, "SDK directory ABI mismatch");
_Static_assert(sizeof(FsTimeStampRaw) == 0x20, "SDK timestamp ABI mismatch");
static void logline(Nx2Host *h, const char *fmt, ...) {
  if (h->lines++ > 10000)
    return;
  va_list a;
  va_start(a, fmt);
  vfprintf(h->report, fmt, a);
  va_end(a);
  fflush(h->report);
}
static Result mkdir_one(FsFileSystem *sd, const char *path) {
  Result rc = fsFsCreateDirectory(sd, path);
  if (R_SUCCEEDED(rc))
    return 0;
  FsDirEntryType type;
  Result q = fsFsGetEntryType(sd, path, &type);
  return R_SUCCEEDED(q) && type == FsDirEntryType_Dir ? 0 : rc;
}
int nx2_host_init(Nx2Host *h, FILE *report, const char *rom,
                  const char *save_key) {
  memset(h, 0, sizeof(*h));
  h->report = report;
  h->sd = fsdevGetDeviceFileSystem("sdmc");
  if (!h->sd || strncmp(rom, "sdmc:", 5))
    return 0;
  if (snprintf(h->rom_root, sizeof(h->rom_root), "%s/romfs", rom + 5) >=
      (int)sizeof(h->rom_root))
    return 0;
  if (!save_key || !save_key[0] ||
      strspn(save_key, "0123456789abcdefyoshi-m") != strlen(save_key) ||
      strchr(save_key, '/') || strchr(save_key, '\\') || strstr(save_key, ".."))
    return 0;
  snprintf(h->save_root, sizeof(h->save_root),
           "/switch/nx2-backup-loader/saves/%s", save_key);
  const char *paths[] = {"/switch/nx2-backup-loader",
                         "/switch/nx2-backup-loader/saves", h->save_root};
  for (unsigned i = 0; i < 3; i++) {
    Result rc = mkdir_one(h->sd, paths[i]);
    logline(h, "PRIVATE_SAVE_DIRECTORY path=%s result=%08x\n", paths[i], rc);
    if (R_FAILED(rc))
      return 0;
  }
  return 1;
}
static uint64_t token(Nx2Host *h, unsigned i) {
  if (!++h->handles[i].generation)
    h->handles[i].generation++;
  return ((uint64_t)h->handles[i].generation << 32) | (i + 1);
}
static Nx2HostFile *lookup(Nx2Host *h, uint64_t id, uint32_t kind) {
  unsigned i = (uint32_t)id;
  if (!i || i > 128)
    return 0;
  Nx2HostFile *f = &h->handles[i - 1];
  return f->kind == kind && f->generation == (uint32_t)(id >> 32) ? f : 0;
}
static Result serve(Nx2Host *h, Nx2Launch *l, Nx2Request *r) {
  char p[1200], p2[1200];
  Result rc = 0;
  Nx2HostFile *f = 0;
  h->calls++;
  r->answer = 0;
  if (r->op == RPC_ROM_READ) {
    if (r->a >= l->rom.count || r->c > 0x10000000)
      return NX2_INVALID;
    unsigned i = (unsigned)r->a;
    const char *relative = nx2_rom_path(&l->rom, i);
    if (!relative || !nx2_save_path(p, sizeof(p), h->rom_root, relative))
      return NX2_INVALID;
    unsigned slot = 16;
    for (unsigned n = 0; n < 16; n++)
      if (h->cache[n].open && h->cache[n].index == i) {
        slot = n;
        break;
      }
    if (slot == 16) {
      slot = 0;
      for (unsigned n = 0; n < 16; n++) {
        if (!h->cache[n].open) {
          slot = n;
          break;
        }
        if (h->cache[n].used < h->cache[slot].used)
          slot = n;
      }
      if (h->cache[slot].open) {
        fsFileClose(&h->cache[slot].file);
        h->cache[slot].open = 0;
      }
      rc = fsFsOpenFile(h->sd, p, FsOpenMode_Read, &h->cache[slot].file);
      if (R_FAILED(rc))
        return rc;
      h->cache[slot].open = 1;
      h->cache[slot].index = i;
      h->cache[slot].logged = 0;
    }
    h->cache[slot].used = h->calls;
    u64 read = 0;
    rc = fsFileRead(&h->cache[slot].file, r->b, r->buffer, r->c,
                    FsReadOption_None, &read);
    h->bytes += read;
    if (!h->cache[slot].logged || R_FAILED(rc)) {
      logline(h,
              "ROM_ASSET_READ file=%s offset=%llu requested=%llu read=%llu "
              "result=%08x\n",
              relative, (unsigned long long)r->b, (unsigned long long)r->c,
              (unsigned long long)read, rc);
      h->cache[slot].logged = 1;
    }
    if (R_SUCCEEDED(rc) && read != r->c)
      rc = NX2_INVALID;
    return rc;
  }
  if (r->op >= RPC_SAVE_CREATE && r->op <= RPC_SAVE_OPEN) {
    if (!nx2_save_path(p, sizeof(p), h->save_root, r->path))
      return NX2_INVALID;
  }
  switch (r->op) {
  case RPC_SAVE_CREATE:
    if (r->a > 0x40000000)
      return NX2_LIMIT;
    rc = fsFsCreateFile(h->sd, p, r->a, 0);
    break;
  case RPC_SAVE_DELETE:
    rc = fsFsDeleteFile(h->sd, p);
    break;
  case RPC_SAVE_MKDIR:
    rc = fsFsCreateDirectory(h->sd, p);
    break;
  case RPC_SAVE_RMDIR:
    if (!strcmp(p, h->save_root))
      return NX2_INVALID;
    rc = fsFsDeleteDirectory(h->sd, p);
    break;
  case RPC_SAVE_RMTREE:
    if (!strcmp(p, h->save_root))
      return NX2_INVALID;
    rc = fsFsDeleteDirectoryRecursively(h->sd, p);
    break;
  case RPC_SAVE_CLEAN:
    rc = fsFsCleanDirectoryRecursively(h->sd, p);
    break;
  case RPC_SAVE_RENAME_FILE:
  case RPC_SAVE_RENAME_DIR:
    if (!nx2_save_path(p2, sizeof(p2), h->save_root, r->path2))
      return NX2_INVALID;
    if (!strcmp(p, h->save_root) || !strcmp(p2, h->save_root))
      return NX2_INVALID;
    rc = r->op == RPC_SAVE_RENAME_FILE ? fsFsRenameFile(h->sd, p, p2)
                                       : fsFsRenameDirectory(h->sd, p, p2);
    break;
  case RPC_SAVE_TYPE: {
    FsDirEntryType type;
    rc = fsFsGetEntryType(h->sd, p, &type);
    if (R_SUCCEEDED(rc))
      r->answer = type;
    break;
  }
  case RPC_SAVE_FREE:
  case RPC_SAVE_TOTAL: {
    s64 amount = 0;
    rc = r->op == RPC_SAVE_FREE ? fsFsGetFreeSpace(h->sd, p, &amount)
                                : fsFsGetTotalSpace(h->sd, p, &amount);
    r->answer = amount;
    break;
  }
  case RPC_SAVE_OPEN: {
    unsigned i = 0;
    for (; i < 128 && h->handles[i].kind; i++)
      ;
    if (i == 128)
      return NX2_LIMIT;
    rc = fsFsOpenFile(h->sd, p, r->a & 7, &h->handles[i].file);
    if (R_SUCCEEDED(rc)) {
      h->handles[i].kind = 1;
      h->handles[i].mode = r->a & 7;
      r->answer = token(h, i);
    }
    break;
  }
  case RPC_SAVE_READ:
  case RPC_SAVE_WRITE:
  case RPC_SAVE_FLUSH:
  case RPC_SAVE_RESIZE:
  case RPC_SAVE_SIZE:
  case RPC_SAVE_CLOSE:
    f = lookup(h, r->a, 1);
    if (!f)
      return NX2_INVALID;
    if (r->op == RPC_SAVE_READ) {
      u64 read = 0;
      if (r->c > 0x10000000)
        return NX2_LIMIT;
      rc = fsFileRead(&f->file, r->b, r->buffer, r->c, 0, &read);
      r->answer = read;
    } else if (r->op == RPC_SAVE_WRITE) {
      if (r->b > 0x40000000 || r->c > 0x40000000 - r->b)
        return NX2_LIMIT;
      rc = fsFileWrite(&f->file, r->b, r->buffer, r->c,
                       (r->d & 1) ? FsWriteOption_Flush : FsWriteOption_None);
      h->save_bytes += r->c;
    } else if (r->op == RPC_SAVE_FLUSH)
      rc = fsFileFlush(&f->file);
    else if (r->op == RPC_SAVE_RESIZE) {
      if (r->b > 0x40000000)
        return NX2_LIMIT;
      rc = fsFileSetSize(&f->file, r->b);
    } else if (r->op == RPC_SAVE_SIZE) {
      s64 size = 0;
      rc = fsFileGetSize(&f->file, &size);
      r->answer = size;
    } else {
      fsFileClose(&f->file);
      f->kind = 0;
    }
    break;
  case RPC_DIR_OPEN: {
    if (!nx2_save_path(p, sizeof(p), h->save_root, r->path))
      return NX2_INVALID;
    unsigned i = 0;
    for (; i < 128 && h->handles[i].kind; i++)
      ;
    if (i == 128)
      return NX2_LIMIT;
    rc = fsFsOpenDirectory(h->sd, p, r->a & 3, &h->handles[i].dir);
    if (R_SUCCEEDED(rc)) {
      h->handles[i].kind = 2;
      r->answer = token(h, i);
    }
    break;
  }
  case RPC_DIR_READ:
  case RPC_DIR_COUNT:
  case RPC_DIR_CLOSE:
    f = lookup(h, r->a, 2);
    if (!f)
      return NX2_INVALID;
    if (r->op == RPC_DIR_READ) {
      s64 n = 0;
      if (r->b > 1024)
        return NX2_LIMIT;
      rc = fsDirRead(&f->dir, &n, r->b, r->buffer);
      r->answer = n;
    } else if (r->op == RPC_DIR_COUNT) {
      s64 n = 0;
      rc = fsDirGetEntryCount(&f->dir, &n);
      r->answer = n;
    } else {
      fsDirClose(&f->dir);
      f->kind = 0;
    }
    break;
  case RPC_SAVE_TIMESTAMP:
    if (!r->buffer || !nx2_save_path(p, sizeof(p), h->save_root, r->path))
      return NX2_INVALID;
    rc = fsFsGetFileTimeStampRaw(h->sd, p, r->buffer);
    break;
  case RPC_COMMIT:
    for (unsigned i = 0; i < 128; i++)
      if (h->handles[i].kind == 1 && (h->handles[i].mode & FsOpenMode_Write)) {
        Result flush = fsFileFlush(&h->handles[i].file);
        if (R_FAILED(flush))
          rc = flush;
      }
    break;
  case RPC_LOG:
    logline(h, "NATIVE_LOG a=%016llx text=%s detail=%s\n",
            (unsigned long long)r->a, r->path, r->path2);
    break;
  case RPC_IPC:
    logline(
        h, "IPC a=%llx command=%llu result=%llx kind=%llu name=%s detail=%s\n",
        (unsigned long long)r->a, (unsigned long long)r->b,
        (unsigned long long)r->c, (unsigned long long)r->d, r->path, r->path2);
    break;
  case RPC_MAIN_ENTERED:
    logline(h, "GAME_NNMAIN_ENTERED: actual nnMain called. This marker does "
               "not prove a rendered title screen.\n");
    break;
  case RPC_ROM_MOUNT:
    logline(h, "ROMFS_MOUNT name=%s result=%08x cache=%llu\n", r->path,
            (unsigned)r->a, (unsigned long long)r->b);
    break;
  case RPC_SAVE_MOUNT:
    logline(h, "PRIVATE_SAVE_MOUNT name=%s result=%08x root=%s\n", r->path,
            (unsigned)r->a, h->save_root);
    break;
  case RPC_PHYSICAL_MEMORY:
    logline(h,
            "NATIVE_PHYSICAL_MEMORY operation=%s phase=%s address=%016llx "
            "bytes=%llu result=%08x\n",
            r->d ? "unmap" : "map", r->c == UINT32_MAX ? "begin" : "end",
            (unsigned long long)r->a, (unsigned long long)r->b, (unsigned)r->c);
    break;
  case RPC_BREAK: {
    logline(h, "SDK_BREAK reason=%llx address=%016llx bytes=%llu\n",
            (unsigned long long)r->a, (unsigned long long)r->b,
            (unsigned long long)r->c);
    if (r->c == 4 && r->b <= UINT64_MAX - 4) {
      MemoryInfo mi;
      u32 page = 0;
      Result q = svcQueryMemory(&mi, &page, r->b);
      if (R_SUCCEEDED(q) && mi.addr <= r->b && mi.size >= 4 &&
          r->b - mi.addr <= mi.size - 4 && (mi.perm & Perm_R)) {
        u32 value = 0;
        memcpy(&value, (const void *)(uintptr_t)r->b, 4);
        logline(h, "SDK_BREAK_WORD value=%08x module=%u description=%u\n",
                value, value & 511, value >> 9);
      } else
        logline(
            h, "SDK_BREAK_WORD unavailable: diagnostic pointer not readable\n");
    }
    h->abort = 1;
    break;
  }
  case RPC_ABORT:
    logline(h,
            "NATIVE_STOP reason=%s a=%016llx b=%016llx c=%016llx d=%016llx\n",
            r->path, (unsigned long long)r->a, (unsigned long long)r->b,
            (unsigned long long)r->c, (unsigned long long)r->d);
    h->abort = 1;
    break;
  default:
    return NX2_FS_UNSUPPORTED;
  }
  if (r->op >= RPC_SAVE_CREATE && r->op <= RPC_COMMIT)
    logline(h, "PRIVATE_SAVE op=%u result=%08x path=%s\n", r->op, rc, r->path);
  return rc;
}
int nx2_host_poll(Nx2Host *h, Nx2Launch *l) {
  int served = 0;
  for (unsigned i = 0; i < NX2_RPC_SLOTS; i++) {
    Nx2Request *r = &l->requests[i];
    if (__atomic_load_n(&r->state, __ATOMIC_ACQUIRE) == 2) {
      r->result = serve(h, l, r);
      __atomic_store_n(&r->state, 3, __ATOMIC_RELEASE);
      __asm__ volatile("sev" ::: "memory");
      served++;
    }
  }
  return served;
}
void nx2_host_close(Nx2Host *h) {
  for (unsigned i = 0; i < 128; i++) {
    if (h->handles[i].kind == 1)
      fsFileClose(&h->handles[i].file);
    if (h->handles[i].kind == 2)
      fsDirClose(&h->handles[i].dir);
    h->handles[i].kind = 0;
  }
  for (unsigned i = 0; i < 16; i++)
    if (h->cache[i].open) {
      fsFileClose(&h->cache[i].file);
      h->cache[i].open = 0;
    }
}
