/* SPDX-License-Identifier: GPL-2.0-only */
#include "host.h"
#include "cache_catalog.h"
#include "nup.h"
#include "path_guard.h"
#include "rom_prefetch.h"
#include "file_range_policy.h"
#include "log_policy.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
_Static_assert(sizeof(FsDirectoryEntry) == 0x310, "SDK directory ABI mismatch");
_Static_assert(sizeof(FsRangeInfo) == 0x40, "SDK range-info ABI mismatch");
_Static_assert(sizeof(FsTimeStampRaw) == 0x20, "SDK timestamp ABI mismatch");
_Static_assert(sizeof(FsFileSystemAttribute) == 0xC0, "SDK filesystem attribute ABI mismatch");
static void logline(Nx2Host *h, const char *fmt, ...) {
  if(!h->report || !nx2_log_keep(fmt))return;
  if(nx2_log_verbose &&
     __atomic_fetch_add(&h->lines,1,__ATOMIC_RELAXED)>10000)return;
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
/* Shared archive marker calculation keeps old metadata fully readable. */
static int archive_marker_for_root(const char *root,char *out,size_t cap){
 uint64_t hash=UINT64_C(14695981039346656037);
 for(const unsigned char *p=(const unsigned char*)root;*p;p++){
  hash^=*p;
  hash*=UINT64_C(1099511628211);
 }
 int n=snprintf(out,cap,"/switch/BackupLoaderMKW/.nx2-archive-%016llx",
                (unsigned long long)hash);
 return n>0&&(size_t)n<cap;
}
/* Preserve a legacy save if it has a marker or ANY directory contents.
 * A pre-created empty mount directory does not pin a title to its old
 * display name. Ambiguous/unreadable legacy data is preserved. */
static int legacy_archive_present(FsFileSystem *sd,const char *root){
 FsDirEntryType type=0;
 if(R_FAILED(fsFsGetEntryType(sd,root,&type))||
    type!=FsDirEntryType_Dir)return 0;
 char marker[120];
 if(!archive_marker_for_root(root,marker,sizeof(marker)))return 1;
 if(R_SUCCEEDED(fsFsGetEntryType(sd,marker,&type))&&
    type==FsDirEntryType_File)return 1;
 FsDir dir;
 if(R_FAILED(fsFsOpenDirectory(sd,root,
              FsDirOpenMode_ReadFiles|FsDirOpenMode_ReadDirs,&dir)))
  return 1;
 FsDirectoryEntry entry;
 s64 count=0;
 Result rc=fsDirRead(&dir,&count,1,&entry);
 fsDirClose(&dir);
 return R_FAILED(rc)||count>0;
}
/* A freshly-created named layout has only saves/ in the game root.
 * Do not let the legacy-flat-save check pin future launches to that parent,
 * or the second launch would silently see a different save. Older flat
 * layouts with ANY other data or an archive marker take precedence. */
static int named_profile_root_present(FsFileSystem *sd,const char *root){
  char path[600],marker[160];
  int n=snprintf(path,sizeof(path),"%s/saves",root);
  if(n<=0||n>=(int)sizeof(path))return 0;
  FsDirEntryType type=0;
  if(R_FAILED(fsFsGetEntryType(sd,path,&type))||type!=FsDirEntryType_Dir)
    return 0;
  if(!archive_marker_for_root(root,marker,sizeof(marker)))return 0;
  if(R_SUCCEEDED(fsFsGetEntryType(sd,marker,&type)))return 0;
  FsDir dir;
  if(R_FAILED(fsFsOpenDirectory(sd,root,
          FsDirOpenMode_ReadFiles|FsDirOpenMode_ReadDirs,&dir)))return 0;
  int only_saves=1;
  for(;;){
    FsDirectoryEntry entry; s64 read=0;
    Result rc=fsDirRead(&dir,&read,1,&entry);
    if(R_FAILED(rc)){only_saves=0;break;}
    if(!read)break;
    if(strcmp(entry.name,"saves")){only_saves=0;break;}
  }
  fsDirClose(&dir);
  return only_saves;
}
int nx2_host_init(Nx2Host *h, FILE *report, const char *rom,
                  const char *save_key,uint64_t guest_id) {
  memset(h, 0, sizeof(*h));
  h->guest_program_id = guest_id;
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
  char old_root[512];
  int n=snprintf(old_root,sizeof(old_root),
                 "/switch/BackupLoaderMKW/%s",save_key);
  if(n<0||n>=(int)sizeof(old_root))return 0;
  /* Preserve every pre-existing title-ID and display-name save, without
   * migrating or deleting the archive/primary-user markers that reference
   * its full pathname. New saves get a human-readable game directory. */
  char title_root[512],game_root[512],save_parent[512];
  n=snprintf(title_root,sizeof(title_root),
             "/switch/BackupLoaderMKW/title-%016llx",
             (unsigned long long)guest_id);
  if(n<0||n>=(int)sizeof(title_root))return 0;
  n=snprintf(game_root,sizeof(game_root),
             "/switch/BackupLoaderMKW/%s",save_key);
  if(n<0||n>=(int)sizeof(game_root))return 0;
  int named_profile=named_profile_root_present(h->sd,game_root);
  int legacy_name=!named_profile && legacy_archive_present(h->sd,old_root);
  int legacy_title=guest_id && legacy_archive_present(h->sd,title_root);
  if(legacy_name || !guest_id)
    n=snprintf(h->save_root,sizeof(h->save_root),"%s",old_root);
  else if(legacy_title)
    n=snprintf(h->save_root,sizeof(h->save_root),"%s",title_root);
  else {
    /* UserIdStorage comes from the homebrew launch environment. No new
     * account service session, attach, or privileged probe is attempted. */
    const AccountUid *selected=envGetUserIdStorage();
    if(selected && (selected->uid[0] || selected->uid[1])) {
      h->launch_uid=(Nx2UserId){selected->uid[0],selected->uid[1]};
      h->launch_uid_valid=1;
    }
    n=snprintf(save_parent,sizeof(save_parent),"%s/saves",game_root);
    if(n<0||n>=(int)sizeof(save_parent))return 0;
    if(h->launch_uid_valid){
      n=snprintf(h->save_root,sizeof(h->save_root),
                 "%s/%016llx%016llx",save_parent,
                 (unsigned long long)h->launch_uid.lo,
                 (unsigned long long)h->launch_uid.hi);
      h->new_save_layout=1;
    }else {
      /* Never substitute the parent directory for an existing per-user
       * tree: that would look like lost save data and risk mixed profiles. */
      if(named_profile){
        logline(h,"WARNING_LAUNCH_USER_UNAVAILABLE existing_profile_tree=1 refusing_unsafe_fallback\n");
        return 0;
      }
      /* For a new game with no HBL UserId, keep the older game-name
       * fallback instead of fabricating a user identity. */
      n=snprintf(h->save_root,sizeof(h->save_root),"%s",game_root);
      logline(h,"WARNING_LAUNCH_USER_UNAVAILABLE source=hbl_environment save_mode=legacy_game_name\n");
    }
  }
  if(n<0||n>=(int)sizeof(h->save_root))return 0;
  logline(h,"SAVE_NAMESPACE strategy=%s guest=%016llx root=%s\n",
          legacy_title?"existing_title_id":
          legacy_name?"existing_game_name":
          h->new_save_layout?"game_name_launch_user":"game_name_unscoped",
          (unsigned long long)guest_id,h->save_root);
  const char *paths[5];
  size_t path_count=0;
  paths[path_count++]="/switch/BackupLoaderMKW";
  if(h->new_save_layout){
    paths[path_count++]=game_root;
    paths[path_count++]=save_parent;
  }
  paths[path_count++]=h->save_root;
  for(size_t i=0;i<path_count;i++){
    Result rc=mkdir_one(h->sd,paths[i]);
    logline(h,"PRIVATE_SAVE_DIRECTORY path=%s result=%08x\n",paths[i],rc);
    if(R_FAILED(rc))return 0;
  }
  return 1;
}
/* Enumerate only real virtual caches belonging to the selected guest.
 * The kernel's fsp-srv cache list belongs to the MKW host process and must
 * never be presented to a foreign guest as its own save metadata. Files
 * are read-only here; creation and durable quota markers remain unchanged. */
static int nx2_cache_index_cmp(const void *a,const void *b){
 uint16_t x=*(const uint16_t*)a,y=*(const uint16_t*)b;
 return x<y?-1:(x>y?1:0);
}
static uint32_t nx2_cache_enumerate(Nx2Host *h,uint64_t cursor,
                                    uint64_t capacity,void *out,uint64_t *count){
 if(!h||!out||!count||!capacity||capacity>256)return NX2_INVALID;
 *count=0;
 char parent[600];
 int n=snprintf(parent,sizeof(parent),"%s/cache-storage",h->save_root);
 if(n<0||n>=(int)sizeof(parent))return NX2_INVALID;
 FsDir dir;
 Result rc=fsFsOpenDirectory(h->sd,parent,FsDirOpenMode_ReadDirs,&dir);
 if(R_FAILED(rc)){
  FsDirEntryType type;
  /* Before CreateCacheStorage there are no virtual caches. An absent
   * folder is a genuine empty catalog, not a successful native call. */
  if(R_FAILED(fsFsGetEntryType(h->sd,parent,&type)))return 0;
  return rc;
 }
 uint16_t *indices=malloc(65536U*sizeof(*indices));
 if(!indices){fsDirClose(&dir);return NX2_LIMIT;}
 size_t used=0;FsDirectoryEntry entry; s64 read=0;
 for(;;){
  rc=fsDirRead(&dir,&read,1,&entry);
  if(R_FAILED(rc)||read==0)break;
  uint16_t index=0;
  if(nx2_cache_parse_index(entry.name,&index) && used<65536)
    indices[used++]=index;
 }
 fsDirClose(&dir);
 if(R_FAILED(rc)){free(indices);return rc;}
 qsort(indices,used,sizeof(*indices),nx2_cache_index_cmp);
 uint64_t skipped=0,produced=0;
 for(size_t i=0;i<used && produced<capacity;i++){
  uint16_t index=indices[i];
  if(i && index==indices[i-1])continue;
  char marker[720];
  n=snprintf(marker,sizeof(marker),"sdmc:%s/.nx2-capacity-%u",
             parent,(unsigned)index);
  if(n<0||n>=(int)sizeof(marker)){free(indices);return NX2_INVALID;}
  FILE *f=fopen(marker,"rb");
  if(!f)continue;
  unsigned char raw[24];
  size_t got=fread(raw,1,sizeof(raw),f);
  int closed=fclose(f);
  uint64_t data_size=0,journal_size=0;
  /* Use the same exact quota validation for enumeration and cmd 34. */
  if(got!=sizeof(raw)||closed||
     !nx2_cache_unpack_quota(raw,sizeof(raw),
                             &data_size,&journal_size))continue;
  if(skipped++<cursor)continue;
  if(!nx2_cache_pack_info((unsigned char*)out+
         produced*NX2_CACHE_INFO_BYTES,NX2_CACHE_INFO_BYTES,
         h->guest_program_id,index,data_size,journal_size)){
   free(indices);return NX2_INVALID;
  }
  produced++;
 }
 free(indices);
 *count=produced;
 return 0;
}
/* Guest save archives are logical objects, not merely directories. The
 * launcher's root directory exists before gameplay for path confinement.
 * A metadata marker OUTSIDE that root indicates a provisioned archive and
 * is never visible as a guest save file. Existing legacy saves (before this
 * marker existed) remain valid if they contain ordinary game content. */
static Result archive_exists_at(Nx2Host *h,const char *root,uint64_t *exists){
  if(!exists)return NX2_INVALID;
  *exists=0;
  char marker[120];
  if(!archive_marker_for_root(root,marker,sizeof(marker)))
    return NX2_INVALID;
  FsDirEntryType type=0;
  Result rc=fsFsGetEntryType(h->sd,marker,&type);
  if(R_SUCCEEDED(rc)){
    if(type!=FsDirEntryType_File)return NX2_INVALID;
    *exists=1;
    return 0;
  }
  /* Legacy saves predate the archive marker. Check for real game content
   * instead of treating the automatically created mount root as a save. */
  FsDir dir;
  rc=fsFsOpenDirectory(h->sd,root,
                        FsDirOpenMode_ReadFiles|FsDirOpenMode_ReadDirs,&dir);
  if(R_FAILED(rc))return rc;
  FsDirectoryEntry entry;
  s64 n=0;
  for(unsigned i=0;i<1024;i++){
    rc=fsDirRead(&dir,&n,1,&entry);
    if(R_FAILED(rc)||n==0)break;
    if(!strcmp(entry.name,"cache-storage")||
       !strcmp(entry.name,"profiles")||
       !strncmp(entry.name,"vsave-",6)||
       !strncmp(entry.name,".nx2-",5))continue;
    *exists=1;
    break;
  }
  fsDirClose(&dir);
  return rc;
}
static Result archive_provision_at(Nx2Host *h,const char *root){
  char marker[120];
  if(!archive_marker_for_root(root,marker,sizeof(marker)))
    return NX2_INVALID;
  FsDirEntryType type=0;
  Result rc=fsFsGetEntryType(h->sd,marker,&type);
  if(R_SUCCEEDED(rc))
    return type==FsDirEntryType_File?0:NX2_INVALID;
  /* CreateFile is atomic in the SD filesystem; if the marker has already
   * appeared due to another save hook, revalidate it and return success. */
  rc=fsFsCreateFile(h->sd,marker,0,0);
  if(R_SUCCEEDED(rc))return 0;
  Result after=fsFsGetEntryType(h->sd,marker,&type);
  return R_SUCCEEDED(after)&&type==FsDirEntryType_File?0:rc;
}
/* Bind the old flat save directory to the first explicitly chosen user.
 * The binding is outside the guest-visible root and never overwritten:
 * subsequent users receive independent subdirectories. */
static Result profile_scope(Nx2Host *h,Nx2UserId uid,int create,
                            char root[620],int *primary){
 if(!h||!primary||!root||!nx2_profile_has_uid(uid))return NX2_INVALID;
 uint64_t hash=UINT64_C(14695981039346656037);
 for(const unsigned char *p=(const unsigned char*)h->save_root;*p;p++){
  hash^=*p;hash*=UINT64_C(1099511628211);
 }
 char marker[160],disk[170];FsDirEntryType type=0;
 int n=snprintf(marker,sizeof(marker),
        "/switch/BackupLoaderMKW/.nx2-primary-%016llx",
        (unsigned long long)hash);
 if(n<0||n>=(int)sizeof(marker))return NX2_INVALID;
 n=snprintf(disk,sizeof(disk),"sdmc:%s",marker);
 if(n<0||n>=(int)sizeof(disk))return NX2_INVALID;
 Result rc=fsFsGetEntryType(h->sd,marker,&type);
 uint64_t record[3]={0};
 if(R_SUCCEEDED(rc)){
  if(type!=FsDirEntryType_File)return NX2_INVALID;
  FILE *f=fopen(disk,"rb");if(!f)return NX2_INVALID;
  size_t count=fread(record,1,sizeof(record),f);int closed=fclose(f);
  if(count!=sizeof(record)||closed||
     record[0]!=NX2_PROFILE_MARKER_MAGIC||
     (!record[1]&&!record[2]))return NX2_INVALID;
 }else if(rc==NX2_NOT_FOUND){
  record[0]=NX2_PROFILE_MARKER_MAGIC;
  record[1]=uid.lo;record[2]=uid.hi;
  FILE *f=fopen(disk,"wb");if(!f)return NX2_INVALID;
  size_t count=fwrite(record,1,sizeof(record),f);
  int flushed=fflush(f),closed=fclose(f);
  if(count!=sizeof(record)||flushed||closed)return NX2_INVALID;
  logline(h,"SAVE_PROFILE_PRIMARY_BOUND old_save_kept_in_place=1\n");
 }else return rc;
 *primary=(record[1]==uid.lo&&record[2]==uid.hi);
 if(*primary){
  n=snprintf(root,620,"%s",h->save_root);
  return n>0&&n<620?0:NX2_INVALID;
 }
 char rel[80];if(!nx2_profile_relative(rel,sizeof(rel),uid))return NX2_INVALID;
 n=snprintf(root,620,"%s/%s",h->save_root,rel);
 if(n<0||n>=620)return NX2_INVALID;
 if(!create)return 0;
 char parent[600];n=snprintf(parent,sizeof(parent),"%s/profiles",h->save_root);
 if(n<0||n>=(int)sizeof(parent))return NX2_INVALID;
 rc=mkdir_one(h->sd,parent);if(R_FAILED(rc))return rc;
 return mkdir_one(h->sd,root);
}
/* Storage requests run on one worker; preserve token index/generation and
 * grow only when another native file/directory handle is really needed. */
static int host_next_free_handle(Nx2Host *h,unsigned *out){
 if(!h||!out)return 0;
 for(size_t i=0;i<h->handle_capacity;i++)
  if(!h->handles[i].kind){*out=(unsigned)i;return 1;}
 size_t old=h->handle_capacity,cap=old?old*2U:64U;
 if(cap<=old||cap>UINT32_MAX||cap>SIZE_MAX/sizeof(*h->handles))return 0;
 Nx2HostFile *p=realloc(h->handles,cap*sizeof(*p));
 if(!p)return 0;
 memset(p+old,0,(cap-old)*sizeof(*p));
 h->handles=p;h->handle_capacity=cap;*out=(unsigned)old;
 return 1;
}
static uint64_t token(Nx2Host *h, unsigned i) {
  if (!++h->handles[i].generation)
    h->handles[i].generation++;
  return ((uint64_t)h->handles[i].generation << 32) | (i + 1);
}
static Nx2HostFile *lookup(Nx2Host *h, uint64_t id, uint32_t kind) {
  unsigned i = (uint32_t)id;
  if (!i || i > h->handle_capacity)
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
    /* The folder backend previously passed an arbitrarily large guest
     * destination to one synchronous native FS IPC. NUP payloads already
     * use bounded operations. Keep exact semantics while limiting each
     * folder IPC to 256 KiB; an incomplete read is NEVER successful. */
    if((r->c&&!r->buffer) || r->b>INT64_MAX ||
       r->c>(uint64_t)INT64_MAX-r->b)return NX2_INVALID;
    u64 read = 0;
    do {
      u64 want=r->c-read;
      if(want>0x40000U)want=0x40000U;
      u64 got=0;
      rc=fsFileRead(&h->cache[slot].file,(s64)(r->b+read),
                    want?(unsigned char*)r->buffer+(size_t)read:r->buffer,
                    want,FsReadOption_None,&got);
      if(R_FAILED(rc))break;
      if(got>want){rc=NX2_INVALID;break;}
      read+=got;
      if(got!=want){rc=NX2_INVALID;break;}
    } while(read<r->c);
    __atomic_fetch_add(&h->bytes, read, __ATOMIC_RELAXED);
    /* A successful-but-short ROMFS IStorage read must never expose stale
     * data in the destination or speculative cache buffer. */
    if(R_SUCCEEDED(rc)&&read!=r->c)rc=NX2_INVALID;
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
  if ((r->op >= RPC_SAVE_CREATE && r->op <= RPC_SAVE_OPEN) ||
      r->op == RPC_SAVE_QUERY_ENTRY) {
    if (!nx2_save_path(p, sizeof(p), h->save_root, r->path))
      return NX2_INVALID;
  }
  switch (r->op) {
  case RPC_SAVE_ARCHIVE_EXISTS: {
    uint64_t exists=0;
    rc=archive_exists_at(h,h->save_root,&exists);
    if(R_SUCCEEDED(rc))r->answer=exists;
    logline(h,"GUEST_SAVE_ARCHIVE_EXISTS result=%08x exists=%llu root=%s\n",
            rc,(unsigned long long)exists,h->save_root);
    break;
  }
  case RPC_SAVE_ARCHIVE_CREATE: {
    rc=archive_provision_at(h,h->save_root);
    logline(h,"GUEST_SAVE_ARCHIVE_CREATE result=%08x root=%s\n",
            rc,h->save_root);
    break;
  }
  case RPC_SAVE_PROFILE_BIND:
  case RPC_SAVE_PROFILE_EXISTS:
  case RPC_SAVE_PROFILE_CREATE: {
    Nx2UserId uid={r->a,r->b};char root[620];int primary=0;
    rc=profile_scope(h,uid,r->op!=RPC_SAVE_PROFILE_EXISTS,root,&primary);
    if(R_SUCCEEDED(rc)){
      r->answer=(uint64_t)primary;
      if(r->op==RPC_SAVE_PROFILE_CREATE)
        rc=archive_provision_at(h,root);
      if(r->op==RPC_SAVE_PROFILE_EXISTS){
        FsDirEntryType type=0;uint64_t exists=0;
        Result q=fsFsGetEntryType(h->sd,root,&type);
        if(q==NX2_NOT_FOUND){rc=0;r->answer=0;}
        else if(R_FAILED(q))rc=q;
        else if(type!=FsDirEntryType_Dir)rc=NX2_INVALID;
        else{
          rc=archive_exists_at(h,root,&exists);
          if(!rc)r->answer=exists;
        }
      }
    }
    logline(h,"SAVE_PROFILE_ROUTE operation=%u result=%08x primary=%d\n",
            r->op,rc,primary);
    break;
  }
  case RPC_VFS_PREPARE_ROOT: {
    /* Never accept a native path or arbitrary directory from the guest.
     * Only a single generated per-profile namespace under the active title.
     * Host directories remain private to this launcher and selected NUP. */
    const char *name=r->path;
    if(strncmp(name,"vsave-",6))return NX2_INVALID;
    size_t len=strlen(name);
    if(len<20||len>110)return NX2_INVALID;
    for(size_t k=6;k<len;k++){
      char c=name[k];
      if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||c=='-'))
        return NX2_INVALID;
    }
    if(!nx2_save_path(p,sizeof(p),h->save_root,name))return NX2_INVALID;
    rc=mkdir_one(h->sd,p);
    logline(h,"GUEST_VFS_PREPARE namespace=[account-scoped] result=%08x\n",rc);
    break;
  }
  case RPC_CACHE_ENUM: {
    uint64_t produced=0;
    rc=nx2_cache_enumerate(h,r->a,r->b,r->buffer,&produced);
    if(R_SUCCEEDED(rc))r->answer=produced;
    logline(h,"GUEST_CACHE_CATALOG cursor=%llu capacity=%llu entries=%llu result=%08x\n",
            (unsigned long long)r->a,(unsigned long long)r->b,
            (unsigned long long)produced,rc);
    break;
  }
  case RPC_CACHE_QUERY_SIZE: {
    /* IFileSystemProxy::GetCacheStorageSize (34): u16 index,
     * two u64 outputs. Read real per-title quota markers, not MKW saves. */
    if(r->a>65535 || r->c!=16 || !r->buffer)return NX2_INVALID;
    char dir[640],marker[720];
    int dn=snprintf(dir,sizeof(dir),"%s/cache-storage/%u",
                    h->save_root,(unsigned)r->a);
    int mn=snprintf(marker,sizeof(marker),
                    "sdmc:%s/cache-storage/.nx2-capacity-%u",
                    h->save_root,(unsigned)r->a);
    if(dn<0||dn>=(int)sizeof(dir)||mn<0||
       mn>=(int)sizeof(marker))return NX2_INVALID;
    FsDirEntryType type;
    rc=fsFsGetEntryType(h->sd,dir,&type);
    if(R_FAILED(rc))return rc;
    if(type!=FsDirEntryType_Dir)return NX2_INVALID;
    FILE *f=fopen(marker,"rb");
    if(!f)return NX2_INVALID;
    unsigned char raw[24];
    size_t got=fread(raw,1,sizeof(raw),f);
    int closed=fclose(f);
    uint64_t data_size=0,journal_size=0;
    if(got!=sizeof(raw)||closed||
       !nx2_cache_unpack_quota(raw,sizeof(raw),&data_size,&journal_size))
      return NX2_INVALID;
    memcpy(r->buffer,raw+8,16);
    break;
  }
  case RPC_CACHE_CREATE:
  case RPC_CACHE_CHECK:
  case RPC_CACHE_PROVISION: {
    /* Cache indexes and quotas are properties of the virtual guest store,
     * not permissions or quotas of the Mario Kart World host title.
     * A native installed title may have pre-provisioned cache archives;
     * NUP execution has no install pass. First-mount provisioning is real,
     * private SD creation, never an invented successful filesystem open. */
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
    if(r->op==RPC_CACHE_CHECK||r->op==RPC_CACHE_PROVISION){
      FsDirEntryType type;
      rc=fsFsGetEntryType(h->sd,dir,&type);
      if(R_SUCCEEDED(rc) && type!=FsDirEntryType_Dir)rc=NX2_INVALID;
      if(r->op==RPC_CACHE_CHECK){
        logline(h,"VIRTUAL_CACHE_CHECK index=%llu result=%08x path=%s\n",
                (unsigned long long)r->a,rc,dir);
        break;
      }
      if(R_SUCCEEDED(rc)){
        /* Preserve existing data and its original per-title quota.
         * A directory without a valid metadata marker is not silently
         * repaired or overwritten; it may contain valuable user data. */
        FILE *f=fopen(marker,"rb");
        unsigned char raw[24];size_t got=0;int closed=-1;
        if(f){got=fread(raw,1,sizeof(raw),f);closed=fclose(f);}
        uint64_t data_size=0,journal_size=0;
        if(!f||got!=sizeof(raw)||closed||
           !nx2_cache_unpack_quota(raw,sizeof(raw),&data_size,&journal_size))
          rc=NX2_INVALID;
        logline(h,"VIRTUAL_CACHE_PROVISION_REUSE index=%llu result=%08x path=%s\n",
                (unsigned long long)r->a,rc,dir);
        break;
      }
      /* Only a genuinely missing cache receives install-style provisioning.
       * Propagate permissions, damaged filesystem and all other errors. */
      if(rc!=NX2_CACHE_NOT_FOUND_RESULT){
        logline(h,"VIRTUAL_CACHE_PROVISION_REFUSED index=%llu result=%08x path=%s\n",
                (unsigned long long)r->a,rc,dir);
        break;
      }
      logline(h,"VIRTUAL_CACHE_PROVISION_MISSING index=%llu data=%llu journal=%llu path=%s\n",
              (unsigned long long)r->a,(unsigned long long)r->b,
              (unsigned long long)r->c,dir);
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
    logline(h,r->op==RPC_CACHE_PROVISION?
            "VIRTUAL_CACHE_INSTALL_PROVISION index=%llu size=%llu journal=%llu free=%lld result=%08x path=%s\n":
            "VIRTUAL_CACHE_CREATE index=%llu size=%llu journal=%llu free=%lld result=%08x path=%s\n",
            (unsigned long long)r->a,(unsigned long long)r->b,
            (unsigned long long)r->c,(long long)free_bytes,rc,dir);
    break;
  }
  case RPC_SAVE_CREATE:
    /* Honor every representable guest file size, subject to actual SD
     * permissions, available space, and filesystem rules. Never silently
     * accept an allocation that did not succeed. */
    if(!nx2_save_create_request_valid(r->a,r->b)){
      logline(h,"SAVE_CREATE_REFUSED reason=invalid_size_or_flags "
                "size=%llu flags=%llx\n",
              (unsigned long long)r->a,(unsigned long long)r->b);
      return NX2_INVALID;
    }
    rc=fsFsCreateFile(h->sd,p,(int64_t)r->a,(uint32_t)r->b);
    if(R_FAILED(rc))
      logline(h,"SAVE_CREATE_FILESYSTEM_REFUSED size=%llu flags=%llx "
                "result=%08x\n",
              (unsigned long long)r->a,(unsigned long long)r->b,rc);
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
    unsigned i=0;
    if(!host_next_free_handle(h,&i))return NX2_LIMIT;
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
  case RPC_SAVE_OPERATE_RANGE:
    f = lookup(h, r->a, 1);
    if (!f)
      return NX2_INVALID;
    if (r->op == RPC_SAVE_READ) {
      if(!nx2_save_read_request_valid(r->b,r->c) ||
         (r->c&&!r->buffer))return NX2_INVALID;
      u64 processed=0;
      /* Real FS reads; 1-MiB batches prevent a huge guest call from
       * monopolizing the storage worker and preserve true short reads. */
      do{
        u64 amount=r->c-processed;
        if(amount>0x100000U)amount=0x100000U;
        u64 got=0;
        rc=fsFileRead(&f->file,(s64)(r->b+processed),
                      amount?(unsigned char*)r->buffer+processed:r->buffer,
                      amount,0,&got);
        if(R_FAILED(rc))break;
        if(got>amount){rc=NX2_INVALID;break;}
        processed+=got;
        if(got<amount)break;
      }while(processed<r->c);
      r->answer=processed;
    } else if (r->op == RPC_SAVE_WRITE) {
      /* Let the real SD filesystem enforce capacity and permissions.
       * Validate signed ABI arithmetic, buffer presence, and options. */
      if (!nx2_save_write_request_valid(r->b,r->c,r->d) ||
          (r->c && !r->buffer))
        return NX2_INVALID;
      rc=fsFileWrite(&f->file,(s64)r->b,r->buffer,r->c,
                     (r->d & 1) ? FsWriteOption_Flush : FsWriteOption_None);
      if(R_SUCCEEDED(rc))h->save_bytes+=r->c;
      else logline(h,"SAVE_WRITE_NATIVE_ERROR offset=%llu bytes=%llu result=%08x\n",
                   (unsigned long long)r->b,
                   (unsigned long long)r->c,rc);
    } else if (r->op == RPC_SAVE_FLUSH)
      rc = fsFileFlush(&f->file);
    else if (r->op == RPC_SAVE_RESIZE) {
      if(!nx2_save_resize_request_valid(r->b))return NX2_INVALID;
      rc=fsFileSetSize(&f->file,(s64)r->b);
      if(R_FAILED(rc))
        logline(h,"SAVE_RESIZE_NATIVE_ERROR size=%llu result=%08x\n",
                (unsigned long long)r->b,rc);
    } else if (r->op == RPC_SAVE_SIZE) {
      s64 size = 0;
      rc = fsFileGetSize(&f->file, &size);
      r->answer = size;
    } else if (r->op == RPC_SAVE_OPERATE_RANGE) {
      /* Native IFile::OperateRange (command 5): pass through the real
       * SD-backed file service instead of returning Unsupported. The
       * guest's 0x40-byte inline response comes from an actual query. */
      if((r->b!=FsOperationId_InvalidateCache &&
          r->b!=FsOperationId_QueryRange) || r->c>INT64_MAX ||
         r->d>INT64_MAX || r->c>INT64_MAX-r->d || !r->buffer)
        return NX2_INVALID;
      FsRangeInfo range={0};
      rc=fsFileOperateRange(&f->file,(FsOperationId)r->b,
                            (s64)r->c,(s64)r->d,&range);
      if(R_SUCCEEDED(rc))memcpy(r->buffer,&range,sizeof(range));
    } else {
      fsFileClose(&f->file);
      f->kind = 0;
    }
    break;
  case RPC_DIR_OPEN: {
    if (!nx2_save_path(p, sizeof(p), h->save_root, r->path))
      return NX2_INVALID;
    unsigned i=0;
    if(!host_next_free_handle(h,&i))return NX2_LIMIT;
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
  case RPC_SAVE_QUERY_ENTRY:
    /* IFileSystem::QueryEntry ID 0 modifies only the virtual guest's
     * SD-backed directory attribute. Never target the namespace root,
     * and never forward a guest path into MKW's fsp-srv. */
    if(r->a!=0 || !strcmp(p,h->save_root))return NX2_INVALID;
    rc=fsFsSetConcatenationFileAttribute(h->sd,p);
    if(R_FAILED(rc))
      logline(h,"VIRTUAL_QUERY_ENTRY_FAILED id=%llu result=%08x path=%s\n",
              (unsigned long long)r->a,rc,p);
    break;
  case RPC_SAVE_ATTRIBUTE:
    if(!r->buffer)return NX2_INVALID;
    rc=fsFsGetFileSystemAttribute(h->sd,(FsFileSystemAttribute *)r->buffer);
    break;
  case RPC_COMMIT:
    for (unsigned i = 0; i < h->handle_capacity; i++)
      if (h->handles[i].kind == 1 && (h->handles[i].mode & FsOpenMode_Write)) {
        Result flush = fsFileFlush(&h->handles[i].file);
        if (R_FAILED(flush))
          rc = flush;
      }
    break;
  case RPC_LOG:
    if(nx2_log_keep(r->path))
      logline(h,nx2_log_verbose?
                "NATIVE_LOG a=%016llx b=%016llx c=%016llx d=%016llx text=%s detail=%s\n":
                "WARNING_NATIVE_EVENT a=%016llx b=%016llx c=%016llx d=%016llx text=%s detail=%s\n",
              (unsigned long long)r->a,(unsigned long long)r->b,
              (unsigned long long)r->c,(unsigned long long)r->d,
              r->path,r->path2);
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
    if(nx2_log_verbose || (r->c!=UINT32_MAX && r->c!=0))
      logline(h,"WARNING_NATIVE_PHYSICAL_MEMORY op=%s address=%016llx "
                "bytes=%llu result=%08x\n",
              r->d?"unmap":"map",(unsigned long long)r->a,
              (unsigned long long)r->b,(unsigned)r->c);
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
  if (r->op >= RPC_SAVE_CREATE && r->op <= RPC_COMMIT){
    const char *visible=r->path;
    if(!strncmp(visible,"vsave-",6)||
        !strncmp(visible,"profiles/",9))visible="[account-scoped]";
    logline(h, "PRIVATE_SAVE op=%u result=%08x path=%s\n", r->op, rc, visible);
  }
  return rc;
}
/* Demand-first streaming across loose folders and raw NUP extents.
 * Four 512 KiB windows prevent unrelated asset streams from evicting a
 * single 1 MiB speculative cache. No game-specific content assumptions. */
static uint64_t rom_extent_length(const Nx2RomView *v,uint32_t file){
 const unsigned char *p=v->entries+32*(size_t)file+8;
 uint64_t n=0;
 for(unsigned j=0;j<8;j++)n|=(uint64_t)p[j]<<(8*j);
 return n;
}
static unsigned rom_select_window(Nx2Host *h,uint32_t file){
 unsigned slot=4;
 for(unsigned i=0;i<4;i++)
  if((h->rom_windows[i].previous||h->rom_windows[i].valid) &&
     h->rom_windows[i].file==file){slot=i;break;}
 if(slot==4){
  for(unsigned i=0;i<4;i++)
   if(!h->rom_windows[i].previous && !h->rom_windows[i].valid){
    slot=i;break;
   }
  if(slot==4){
   slot=0;
   for(unsigned i=1;i<4;i++)
    if(h->rom_windows[i].stamp<h->rom_windows[slot].stamp)slot=i;
  }
  h->rom_windows[slot].file=file;
  h->rom_windows[slot].valid=0;
  h->rom_windows[slot].previous=0;
  h->rom_windows[slot].streak=0;
  h->rom_windows[slot].length=0;
 }
 h->rom_windows[slot].stamp=++h->rom_window_clock;
 return slot;
}
static uint32_t serve_rom_cached(Nx2Host *h,Nx2Launch *l,Nx2Request *r){
 if(r->a>=l->rom.count || r->c>0x10000000U ||
    !r->c || (!r->buffer&&r->c))return serve(h,l,r);
 const uint32_t file=(uint32_t)r->a;
 const uint64_t offset=r->b,bytes=r->c;
 const uint64_t length=rom_extent_length(&l->rom,file);
 if(offset>length || bytes>length-offset)return serve(h,l,r);
 unsigned slot=rom_select_window(h,file);
 unsigned streak=h->rom_windows[slot].previous &&
                 h->rom_windows[slot].last_end==offset
    ?h->rom_windows[slot].streak+1:0;
 if(streak>64)streak=64;
 uint32_t result=NX2_INVALID;
 if(h->rom_windows[slot].valid && h->rom_windows[slot].data &&
    offset>=h->rom_windows[slot].offset &&
    offset-h->rom_windows[slot].offset<=h->rom_windows[slot].length &&
    bytes<=h->rom_windows[slot].length-
           (offset-h->rom_windows[slot].offset)){
  memcpy(r->buffer,h->rom_windows[slot].data+
         (size_t)(offset-h->rom_windows[slot].offset),(size_t)bytes);
  __atomic_fetch_add(&h->calls,1,__ATOMIC_RELAXED);
  __atomic_fetch_add(&h->bytes,bytes,__ATOMIC_RELAXED);
  __atomic_fetch_add(&h->read_ahead_hits,1,__ATOMIC_RELAXED);
  result=0;
 }else{
  uint64_t amount=nx2_rom_prefetch_bytes(bytes,length-offset,streak);
  if(amount && nx2_nup_active()){
   /* Random-seek speculative reads are unsafe for compressed NUP extents. */
   char name[1200];unsigned codec=UINT32_MAX;uint64_t unpacked=0;
   const char *relative=nx2_rom_path(&l->rom,file);
   int n=relative?snprintf(name,sizeof(name),"romfs/%s",relative):-1;
   if(n<0||n>=(int)sizeof(name) ||
      !nx2_nup_file_info(name,&unpacked,&codec)||codec!=0)
    amount=0;
  }
  if(amount && !h->rom_windows[slot].data)
   h->rom_windows[slot].data=malloc(0x80000);
  if(amount && h->rom_windows[slot].data){
   Nx2Request temp=*r;
   temp.buffer=h->rom_windows[slot].data;
   temp.c=amount;
   result=serve(h,l,&temp);
   if(!result){
    memcpy(r->buffer,h->rom_windows[slot].data,(size_t)bytes);
    h->rom_windows[slot].offset=offset;
    h->rom_windows[slot].length=amount;
    h->rom_windows[slot].valid=1;
    __atomic_fetch_add(&h->read_ahead_fills,1,__ATOMIC_RELAXED);
    /* The guest received bytes, not the speculative suffix. */
    __atomic_fetch_sub(&h->bytes,amount-bytes,__ATOMIC_RELAXED);
   }else{
    /* Failed speculative suffix is never an error for a valid prefix. */
    h->rom_windows[slot].valid=0;
    result=serve(h,l,r);
   }
  }else result=serve(h,l,r);
 }
 if(!result){
  h->rom_windows[slot].last_end=offset+bytes;
  h->rom_windows[slot].previous=1;
  h->rom_windows[slot].streak=streak;
 }else{
  h->rom_windows[slot].previous=0;
  h->rom_windows[slot].streak=0;
 }
 return result;
}
int nx2_host_poll(Nx2Host *h, Nx2Launch *l) {
  int served = 0;
  for (unsigned i = 0; i < NX2_RPC_SLOTS; i++) {
    Nx2Request *r = &l->requests[i];
    if (__atomic_load_n(&r->state, __ATOMIC_ACQUIRE) == 2) {
      __atomic_store_n(&h->rpc_active_op,r->op,__ATOMIC_RELAXED);
      __atomic_store_n(&h->rpc_started_tick,armGetSystemTick(),__ATOMIC_RELAXED);
      __atomic_store_n(&h->rpc_active_slot,i+1,__ATOMIC_RELEASE);
      r->result = r->op==RPC_ROM_READ ?
          serve_rom_cached(h,l,r) : serve(h,l,r);
      /* A successful I/O request is not news. A failed one must survive
       * quiet logging even if its normal name contains no ERROR token.
       * Sample repetitive failures to avoid amplifying a failure storm. */
      if(r->result){
        static unsigned failures;
        unsigned occurrence=++failures;
        if(occurrence<=32 || !(occurrence&255U))
          logline(h,"ERROR_RPC_COMPLETION op=%u result=%08x slot=%u "
                    "occurrence=%u a=%016llx b=%016llx c=%016llx\n",
                  r->op,r->result,i,occurrence,
                  (unsigned long long)r->a,(unsigned long long)r->b,
                  (unsigned long long)r->c);
      }
      /* Do not issue SD writes for every RomFS read and host RPC.
       * Publish the result immediately; existing RPC_TRAIL breadcrumbs,
       * private save operation results and backend failure logs remain.
       * Previous per-operation fprintf() stalled both Yoshi variants. */
      __atomic_store_n(&r->state, 3, __ATOMIC_RELEASE);
      __atomic_fetch_add(&h->rpc_completed_count,1,__ATOMIC_RELAXED);
      __atomic_store_n(&h->rpc_active_slot,0,__ATOMIC_RELEASE);
      __asm__ volatile("sev" ::: "memory");
      served++;
    }
  }
  return served;
}
void nx2_host_close(Nx2Host *h) {
  for(unsigned i=0;i<4;i++){
    free(h->rom_windows[i].data);
    h->rom_windows[i].data=0;
    h->rom_windows[i].valid=0;
  }
  for (unsigned i = 0; i < h->handle_capacity; i++) {
    if (h->handles[i].kind == 1)
      fsFileClose(&h->handles[i].file);
    if (h->handles[i].kind == 2)
      fsDirClose(&h->handles[i].dir);
    h->handles[i].kind = 0;
  }
  free(h->handles);
  h->handles=0;h->handle_capacity=0;
  for (unsigned i = 0; i < 16; i++)
    if (h->cache[i].open) {
      fsFileClose(&h->cache[i].file);
      h->cache[i].open = 0;
    }
}
