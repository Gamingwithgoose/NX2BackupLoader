/* SPDX-License-Identifier: GPL-2.0-only */
#include "host.h"
#include "nup.h"
#include "path_guard.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
_Static_assert(sizeof(FsDirectoryEntry) == 0x310, "SDK directory ABI mismatch");
_Static_assert(sizeof(FsTimeStampRaw) == 0x20, "SDK timestamp ABI mismatch");
static void logline(Nx2Host *h, const char *fmt, ...) {
  if (__atomic_fetch_add(&h->lines, 1, __ATOMIC_RELAXED) > 10000)
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
  if (!save_key || !save_key[0] || !strcmp(save_key, ".") ||
      !strcmp(save_key, "..") || strchr(save_key, '/') ||
      strchr(save_key, '\\') || strchr(save_key, ':'))
    return 0;
  int n = snprintf(h->save_root, sizeof(h->save_root),
                   "/switch/BackupLoaderMKW/%s", save_key);
  if (n < 0 || n >= (int)sizeof(h->save_root))
    return 0;
  const char *paths[] = {"/switch/BackupLoaderMKW", h->save_root};
  for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
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
  __atomic_fetch_add(&h->calls, 1, __ATOMIC_RELAXED);
  r->answer = 0;
  if (r->op == RPC_ROM_READ) {
    if (r->a >= l->rom.count || r->c > 0x10000000)
      return NX2_INVALID;
    unsigned i = (unsigned)r->a;
    const char *relative = nx2_rom_path(&l->rom, i);
    if (!relative) return NX2_INVALID;
    if(nx2_nup_active()) {
      if(snprintf(p,sizeof(p),"romfs/%s",relative)>=(int)sizeof(p))
        return NX2_INVALID;
      uint64_t entry_size=0;
      unsigned codec=0;
      int large=nx2_nup_file_info(p,&entry_size,&codec) &&
                codec==1 && entry_size>16*1024*1024;
      int sampled=large && (++h->nup_large_reads<=8 ||
                   !(h->nup_large_reads&63));
      /* Flush the start of unusually expensive reads before decoding, so
       * a long-running compressed frame still leaves useful evidence. */
      if(sampled)
        logline(h,"NUP_LARGE_READ_BEGIN file=%s entry_bytes=%llu offset=%llu bytes=%llu\n",
                relative,(unsigned long long)entry_size,
                (unsigned long long)r->b,(unsigned long long)r->c);
      if(!nx2_nup_read(p,r->b,r->buffer,(size_t)r->c)){
        logline(h,"NUP_ASSET_READ_FAILED file=%s offset=%llu bytes=%llu reason=%s\n",
                relative,(unsigned long long)r->b,(unsigned long long)r->c,
                nx2_nup_read_error());
        return NX2_INVALID;
      }
      __atomic_fetch_add(&h->bytes, r->c, __ATOMIC_RELAXED);
      if(sampled)
        logline(h,"NUP_LARGE_READ_DONE file=%s offset=%llu bytes=%llu\n",
                relative,(unsigned long long)r->b,(unsigned long long)r->c);
      else if(h->calls<64)
        logline(h,"NUP_ASSET_READ file=%s offset=%llu bytes=%llu\n",
                relative,(unsigned long long)r->b,(unsigned long long)r->c);
      return 0;
    }
    if (!nx2_save_path(p, sizeof(p), h->rom_root, relative))
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
    __atomic_fetch_add(&h->bytes, read, __ATOMIC_RELAXED);
    /* Startup may read hundreds of files in parallel. Every successful
     * asset log is an SD-card flush on the host and a blocking guest RPC.
     * Keep initial examples, periodic samples and every failure, but stop
     * turning routine resource loading into synchronous log traffic. */
    if (R_FAILED(rc) || read!=r->c ||
        (!h->cache[slot].logged &&
         (h->calls<96 || !(h->calls & 127)))) {
      logline(h,
              "ROM_ASSET_READ file=%s offset=%llu requested=%llu read=%llu "
              "result=%08x\n",
              relative, (unsigned long long)r->b, (unsigned long long)r->c,
              (unsigned long long)read, rc);
    }
    h->cache[slot].logged = 1;
    if (R_SUCCEEDED(rc) && read != r->c)
      rc = NX2_INVALID;
    return rc;
  }
  if (r->op >= RPC_SAVE_CREATE && r->op <= RPC_SAVE_OPEN) {
    if (!nx2_save_path(p, sizeof(p), h->save_root, r->path))
      return NX2_INVALID;
  }
  switch (r->op) {
  case RPC_CACHE_CREATE:
  case RPC_CACHE_CHECK: {
    /* Cache indexes and quotas are properties of the virtual guest store,
     * not permissions or quotas of the Mario Kart World host title. */
    if(r->a>65535 || r->b>INT64_MAX || r->c>INT64_MAX ||
       r->b>INT64_MAX-r->c)return NX2_INVALID;
    char parent[600],dir[640],marker[700];
    int x=snprintf(parent,sizeof(parent),"%s/cache-storage",h->save_root);
    int y=snprintf(dir,sizeof(dir),"%s/%llu",parent,
                   (unsigned long long)r->a);
    /* Keep quota bookkeeping outside the directory visible to the guest. */
    int z=snprintf(marker,sizeof(marker),"sdmc:%s/.nx2-capacity-%llu",
                   parent,(unsigned long long)r->a);
    if(x<0||x>=(int)sizeof(parent)||y<0||y>=(int)sizeof(dir)||
       z<0||z>=(int)sizeof(marker))return NX2_INVALID;
    if(r->op==RPC_CACHE_CHECK){
      FsDirEntryType type;
      rc=fsFsGetEntryType(h->sd,dir,&type);
      if(R_SUCCEEDED(rc) && type!=FsDirEntryType_Dir)rc=NX2_INVALID;
      logline(h,"VIRTUAL_CACHE_CHECK index=%llu result=%08x path=%s\n",
              (unsigned long long)r->a,rc,dir);
      break;
    }
    s64 free_bytes=0;
    rc=fsFsGetFreeSpace(h->sd,h->save_root,&free_bytes);
    if(R_FAILED(rc)){
      logline(h,"VIRTUAL_CACHE_FREE_SPACE_FAILED result=%08x\n",rc);
      break;
    }
    if(free_bytes<0 || r->b+r->c>(uint64_t)free_bytes){
      rc=NX2_LIMIT;
      logline(h,"VIRTUAL_CACHE_SPACE_REFUSED index=%llu size=%llu journal=%llu free=%lld\n",
              (unsigned long long)r->a,(unsigned long long)r->b,
              (unsigned long long)r->c,(long long)free_bytes);
      break;
    }
    rc=mkdir_one(h->sd,parent);
    if(R_SUCCEEDED(rc))rc=mkdir_one(h->sd,dir);
    if(R_SUCCEEDED(rc)){
      /* Record the requested logical capacity persistently. Data files are
       * allocated on demand, and subsequent writes still use real SD errors. */
      uint64_t quota[]={UINT64_C(0x31534E4532484343),r->b,r->c};
      FILE *f=fopen(marker,"wb");
      if(!f)rc=NX2_INVALID;
      else{
        if(fwrite(quota,1,sizeof(quota),f)!=sizeof(quota)||fflush(f))
          rc=NX2_INVALID;
        if(fclose(f))rc=NX2_INVALID;
      }
    }
    logline(h,"VIRTUAL_CACHE_CREATE index=%llu size=%llu journal=%llu free=%lld result=%08x path=%s\n",
            (unsigned long long)r->a,(unsigned long long)r->b,
            (unsigned long long)r->c,(long long)free_bytes,rc,dir);
    break;
  }
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
    logline(h, "NATIVE_LOG a=%016llx b=%016llx c=%016llx d=%016llx text=%s detail=%s\n",
            (unsigned long long)r->a, (unsigned long long)r->b,
            (unsigned long long)r->c, (unsigned long long)r->d,
            r->path, r->path2);
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
    if(r->a==0){
      __atomic_store_n(&l->late_trace_active,1,__ATOMIC_RELEASE);
      logline(h,"LATE_STARTUP_TRACE_ARMED after_successful_save_mount=1\n");
    }
    break;
  case RPC_PHYSICAL_MEMORY:
    logline(h,
            "NATIVE_PHYSICAL_MEMORY operation=%s phase=%s address=%016llx "
            "bytes=%llu result=%08x\n",
            r->d ? "unmap" : "map", r->c == UINT32_MAX ? "begin" : "end",
            (unsigned long long)r->a, (unsigned long long)r->b, (unsigned)r->c);
    break;
  case RPC_BREAK: {
    /* Report native SDK breaks uniformly for every guest title. */
    logline(h, "SDK_BREAK reason=%llx address=%016llx bytes=%llu caller=%016llx\n",
            (unsigned long long)r->a, (unsigned long long)r->b,
            (unsigned long long)r->c,(unsigned long long)r->d);
    for(unsigned region=0;region<l->region_count;region++){
      Nx2Region *m=&l->regions[region];
      if(r->d>=m->begin && r->d<m->end){
        logline(h,"SDK_BREAK_CALLER_REGION index=%u region_base=%016llx offset=%llx type=%u\n",
                region,(unsigned long long)m->begin,
                (unsigned long long)(r->d-m->begin),m->type);
        break;
      }
    }
    /* Capture a few executable instructions before the Break thunk caller.
     * Do not read a guest pointer without verifying its mapped permission. */
    if(r->d>=16 && r->d<=UINT64_MAX-16){
      MemoryInfo code;u32 page=0;
      uint64_t at=(r->d-16)&~UINT64_C(3);
      Result q=svcQueryMemory(&code,&page,at);
      if(R_SUCCEEDED(q) && code.addr<=at &&
         code.size>=32 && at-code.addr<=code.size-32 &&
         (code.perm&Perm_R)){
        const uint32_t *words=(const uint32_t *)(uintptr_t)at;
        logline(h,"SDK_BREAK_CODE start=%016llx words=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
                (unsigned long long)at,words[0],words[1],words[2],words[3],
                words[4],words[5],words[6],words[7]);
      }else{
        logline(h,"SDK_BREAK_CODE unavailable query=%08x\n",q);
      }
    }
    /* Stack scan is diagnostic only. Validate the readable portion before
     * following any pointers and report only values inside guest code.
     * Captures callers above the patched SVC wrapper without changing the
     * game's execution or suppressing its break. */
    uintptr_t sp=(uintptr_t)r->buffer;
    if(sp && !(sp&7)){
      MemoryInfo stack;u32 page=0;
      Result q=svcQueryMemory(&stack,&page,sp);
      logline(h,"SDK_BREAK_STACK fp=%016llx query=%08x type=%u perm=%u\n",
              (unsigned long long)sp,q,
              R_SUCCEEDED(q)?stack.type:0,
              R_SUCCEEDED(q)?stack.perm:0);
      if(R_SUCCEEDED(q) && stack.addr<=sp &&
         stack.size<=UINT64_MAX-stack.addr &&
         sp<stack.addr+stack.size && (stack.perm&Perm_R)){
        /* Follow the AArch64 [previous x29, saved x30] frame chain,
         * instead of interpreting incidental stack values as callers.
         * Only accept aligned, increasing links in the same mapped stack. */
        uint64_t stack_end=stack.addr+stack.size;
        uintptr_t frame=sp;
        for(unsigned depth=0;depth<24;depth++){
          if((frame&15) || frame<stack.addr ||
             frame>stack_end || stack_end-frame<16)break;
          const uint64_t *record=(const uint64_t *)(uintptr_t)frame;
          uint64_t next=record[0],return_pc=record[1];
          /* Saved LR can contain AArch64 pointer-authentication bits. The
           * native guest lives below the loader's 0x8000000000 VA boundary.
           * Try the raw address first; only normalize if it matches a known
           * executable region. Retain the original for unknown frames. */
          uint64_t candidates[2]={return_pc,return_pc & UINT64_C(0x7fffffffff)};
          int found=0;
          for(unsigned variant=0;variant<2&&!found;variant++){
            if(variant && candidates[variant]==candidates[0])break;
            for(uint32_t j=0;j<l->region_count;j++){
              const Nx2Region *m=&l->regions[j];
              if(m->permission==5 && candidates[variant]>=m->begin &&
                 candidates[variant]<m->end){
                logline(h,
                  "SDK_BREAK_FRAME depth=%u region=%u code_offset=%llx pac_stripped=%u\n",
                  depth,j,(unsigned long long)(candidates[variant]-m->begin),variant);
                found=1;
                break;
              }
            }
          }
          if(!found)
            logline(h,"SDK_BREAK_FRAME depth=%u return=%016llx (not guest code)\n",
                    depth,(unsigned long long)return_pc);
          if(next<=frame || next>stack_end || next-frame>1024*1024)
            break;
          frame=(uintptr_t)next;
        }
        uint64_t accessible=(stack.addr+stack.size)-sp;
        if(accessible>2048)accessible=2048;
        const uint64_t *words=(const uint64_t *)sp;
        unsigned matches=0;
        for(uint64_t i=0;i<accessible/8 && matches<24;i++){
          uintptr_t candidate=(uintptr_t)words[i];
          if(candidate&3)continue;
          for(uint32_t j=0;j<l->region_count;j++){
            const Nx2Region *m=&l->regions[j];
            if(m->permission==5 && candidate>=m->begin &&
               candidate<m->end){
              logline(h,
                "SDK_BREAK_STACK_CODE stack_offset=%llu region=%u code_offset=%llx\n",
                (unsigned long long)(i*8),j,
                (unsigned long long)(candidate-m->begin));
              matches++;
              break;
            }
          }
        }
      }
    }
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
