/* SPDX-License-Identifier: GPL-2.0-only */
#include "folder.h"
#include "nup.h"
#include "nupparts.h"
#ifdef __SWITCH__
#include <switch.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir(path,mode) _mkdir(path)
#define STAT _stat64
#else
#include <dirent.h>
#define STAT stat
#endif
static uint32_t resume_files;
uint32_t nx2_folder_resumed_files(void) {return resume_files;}
static int (*cancel_callback)(void *);
static void *cancel_context;
void nx2_folder_cancel(int (*fn)(void *), void *ctx) {
  cancel_callback = fn;
  cancel_context = ctx;
}
static void (*progress_callback)(uint32_t, void *);
static void *progress_context;
void nx2_folder_progress(void (*fn)(uint32_t, void *), void *ctx) {
  progress_callback = fn;
  progress_context = ctx;
}
typedef int (*Visit)(void *, const char *);
static int list(const char *root, Visit visit, void *ctx) {
#ifdef _WIN32
  char pattern[1600];
  if (snprintf(pattern, sizeof(pattern), "%s/*", root) >= (int)sizeof(pattern))
    return 0;
  WIN32_FIND_DATAA e;
  HANDLE h = FindFirstFileA(pattern, &e);
  if (h == INVALID_HANDLE_VALUE)
    return 0;
  int ok = 1;
  do {
    if (strcmp(e.cFileName, ".") && strcmp(e.cFileName, "..") &&
        !visit(ctx, e.cFileName)) {
      ok = 0;
      break;
    }
  } while (FindNextFileA(h, &e));
  FindClose(h);
  return ok;
#else
  DIR *d = opendir(root);
  if (!d)
    return 0;
  struct dirent *e;
  int ok = 1;
  while ((e = readdir(d)))
    if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..") &&
        !visit(ctx, e->d_name)) {
      ok = 0;
      break;
    }
  closedir(d);
  return ok;
#endif
}
static int type(const char *p, int dir) {
  struct STAT st;
  return !STAT(p, &st) && ((st.st_mode & S_IFMT) == (dir ? S_IFDIR : S_IFREG));
}
static uint32_t read_le32(const void *p) {
  const unsigned char *s = p;
  return (uint32_t)s[0] | (uint32_t)s[1] << 8 | (uint32_t)s[2] << 16 |
         (uint32_t)s[3] << 24;
}
static uint64_t read_le64(const void *p) {
  const unsigned char *s = p;
  return read_le32(s) | (uint64_t)read_le32(s + 4) << 32;
}
static void p32(void *p, uint32_t v) {
  unsigned char *s = p;
  for (unsigned i = 0; i < 4; i++)
    s[i] = (unsigned char)(v >> (8 * i));
}
static void p64(void *p, uint64_t v) {
  unsigned char *s = p;
  for (unsigned i = 0; i < 8; i++)
    s[i] = (unsigned char)(v >> (8 * i));
}
static int child(char *out, size_t cap, const char *root, const char *name) {
  int n = snprintf(out, cap, "%s/%s", root, name);
  return n >= 0 && (size_t)n < cap;
}
int nx2_folder_modules(const char *root, char names[13][16], unsigned *count) {
  if(nx2_nup_active()) {
    const char *required[]={"rtld","main"};
    uint64_t size=0; unsigned n=0;
    for(unsigned i=0;i<2;i++) {
      char path[64];snprintf(path,sizeof(path),"exefs/%s",required[i]);
      if(!nx2_nup_file_info(path,&size,NULL))return 0;
      strcpy(names[n++],required[i]);
    }
    for(unsigned i=0;i<10;i++) {
      char path[64];snprintf(path,sizeof(path),"exefs/subsdk%u",i);
      if(nx2_nup_file_info(path,&size,NULL))
        snprintf(names[n++],16,"subsdk%u",i);
    }
    if(!nx2_nup_file_info("exefs/sdk",&size,NULL))return 0;
    strcpy(names[n++],"sdk");*count=n;return 1;
  }
  char path[1400];
  unsigned n = 0;
  const char *first[] = {"rtld", "main"};
  for (unsigned i = 0; i < 2; i++) {
    if (!child(path, sizeof(path), root, "exefs") ||
        strlen(path) + 1 + strlen(first[i]) >= sizeof(path))
      return 0;
    strcat(path, "/");
    strcat(path, first[i]);
    if (!type(path, 0))
      return 0;
    strcpy(names[n++], first[i]);
  }
  for (unsigned i = 0; i < 10; i++) {
    char tail[40];
    snprintf(tail, sizeof(tail), "exefs/subsdk%u", i);
    if (!child(path, sizeof(path), root, tail))
      return 0;
    if (type(path, 0))
      snprintf(names[n++], 16, "subsdk%u", i);
  }
  if (!child(path, sizeof(path), root, "exefs/sdk") || !type(path, 0))
    return 0;
  strcpy(names[n++], "sdk");
  *count = n;
  return 1;
}
/* A dump may keep only ExeFS under base/ while a numbered update owns
 * its complete ExeFS and RomFS. Resolve the highest complete update before
 * rejecting the base. No files are moved or installed. */
typedef struct {
  char directory[1200];
  char selected[1200];
  uint64_t version;
  int found;
} Nx2UpdateChoice;
static int nx2_complete_content_root(const char *root){
  char path[1400],modules[13][16];unsigned count=0;
  return child(path,sizeof(path),root,"romfs")&&type(path,1)&&
         child(path,sizeof(path),root,"exefs/main.npdm")&&type(path,0)&&
         nx2_folder_modules(root,modules,&count);
}
static int nx2_find_versioned_update(void *arg,const char *name){
  Nx2UpdateChoice *c=(Nx2UpdateChoice*)arg;
  if(!name[0])return 1;
  uint64_t v=0;
  for(const unsigned char *p=(const unsigned char*)name;*p;p++){
    if(*p<'0'||*p>'9')return 1;
    if(v>(UINT64_MAX-(uint64_t)(*p-'0'))/10)return 1;
    v=v*10+(uint64_t)(*p-'0');
  }
  char candidate[1200];
  if(!child(candidate,sizeof(candidate),c->directory,name)||
     !nx2_complete_content_root(candidate))return 1;
  if(!c->found||v>c->version){
    strcpy(c->selected,candidate);
    c->version=v;c->found=1;
  }
  return 1;
}
static int nx2_choose_update(const char *folder,Nx2UpdateChoice *selected){
  memset(selected,0,sizeof(*selected));
  const char *variants[]={"updates","update"};
  for(unsigned i=0;i<2;i++){
    if(!child(selected->directory,sizeof(selected->directory),folder,variants[i]))
      continue;
    if(!type(selected->directory,1))continue;
    /* A single update may directly contain exefs/romfs. Prefer numbered
     * updates when available, otherwise accept this complete root. */
    Nx2UpdateChoice local={0};
    strcpy(local.directory,selected->directory);
    list(local.directory,nx2_find_versioned_update,&local);
    if(local.found&&(!selected->found||local.version>selected->version)){
      strcpy(selected->selected,local.selected);
      selected->version=local.version;selected->found=1;
    }else if(!selected->found&&nx2_complete_content_root(local.directory)){
      strcpy(selected->selected,local.directory);
      selected->version=0;selected->found=1;
    }
  }
  return selected->found;
}
typedef struct {
  const char *root;
  Nx2Catalog *out;
} CatalogCtx;
static int catalog_entry(void *v, const char *name) {
  CatalogCtx *c = v;
  if (name[0] == '.' || strlen(name) >= 256 || strchr(name, '\\') ||
      strchr(name, ':'))
    return 1;
  char folder[1200];
  if (!child(folder, sizeof(folder), c->root, name))
    return 1;
  size_t len = strlen(name);
  NupPart *parts=calloc(NUP_PARTS_MAX,sizeof(*parts));
  if(!parts)return 0;
  int count=nup_parts_entry(c->root,name,type(folder,1),parts,NUP_PARTS_MAX);
  free(parts);
  if(count>0) {
    uint64_t id = 0, files = 0;
    int valid = nx2_nup_probe(folder, &id, &files);
    Nx2Game archive = {0};
    NupPartName part;
    if(!type(folder,1)&&nup_part_name(name,&part)&&count>1){
      snprintf(archive.name,sizeof(archive.name),"%s",part.stem);
      size_t s=strlen(archive.name);if(nup_name_is_nup(archive.name))archive.name[s-4]=0;
    }else{
      size_t s=nup_name_is_nup(name)?len-4:len;
      memcpy(archive.name,name,s);archive.name[s]=0;
    }
    strcpy(archive.root, folder);
    strcpy(archive.library_root,folder);
    archive.id = id;
    archive.is_nup = valid ? 1 : 2;
    snprintf(archive.nup_status, sizeof(archive.nup_status),
             "%s", nx2_nup_probe_error());
    if (c->out->count >= 4096) return 0;
    void *items = realloc(c->out->items,
                          (c->out->count + 1) * sizeof(archive));
    if (!items) return 0;
    c->out->items = items;
    c->out->items[c->out->count++] = archive;
    return 1;
  }
  return 1; /* NUP-only library. */
}
static int game_compare(const void *a, const void *b) {
  return strcmp(((const Nx2Game *)a)->name, ((const Nx2Game *)b)->name);
}
int nx2_catalog(const char *root, Nx2Catalog *out) {
  *out = (Nx2Catalog){0};
  CatalogCtx c = {root, out};
  if (!list(root, catalog_entry, &c)) {
    nx2_catalog_free(out);
    return 0;
  }
  qsort(out->items, out->count, sizeof(*out->items), game_compare);
  return 1;
}
void nx2_catalog_free(Nx2Catalog *c) {
  free(c->items);
  *c = (Nx2Catalog){0};
}

typedef struct {
  char *path;
  uint64_t size, data;
  uint32_t at, parent, next, dir, file, hash, po, source;
} Entry;
typedef struct {
  Entry *files, *dirs;
  uint32_t nf, nd, fc, dc;
  const char *root;
  char relative[1024];
  unsigned depth;
  int failed,layered;
} Tree;
static uint64_t align64(uint64_t n, uint64_t a) {
  return (n + a - 1) & ~(a - 1);
}
static int add(Entry **items, uint32_t *count, uint32_t *capacity,
               const char *path, uint64_t bytes) {
  const uint32_t limit=(128U*1024*1024)/sizeof(Entry);
  if (*count >= limit)
    return 0;
  if (*count == *capacity) {
    uint32_t cap = *capacity ? *capacity * 2 : 64;
    if(cap>limit)cap=limit;
    void *p = realloc(*items, (size_t)cap * sizeof(Entry));
    if (!p)
      return 0;
    *items = p;
    *capacity = cap;
  }
  char *s = malloc(strlen(path) + 1);
  if (!s)
    return 0;
  strcpy(s, path);
  (*items)[(*count)++] = (Entry){.path = s,
                                 .size = bytes,
                                 .next = UINT32_MAX,
                                 .dir = UINT32_MAX,
                                 .file = UINT32_MAX};
  return 1;
}
/* Scan only the children of the current directory. The queue is walked
 * separately so completed directories can be checkpointed and skipped. */
static int tree_entry(void *v, const char *name) {
  Tree *t = v;
  if (cancel_callback && cancel_callback(cancel_context)) return 0;
  if (!strcmp(name, ".listing")) return 1;
  if (strchr(name, ':') || strchr(name, '\\')) return 0;
  size_t old = strlen(t->relative);
  char rel[1024], full[2400];
  int n = snprintf(rel, sizeof(rel), "%s%s%s", t->relative,
                   old ? "/" : "", name);
  if (n < 0 || (size_t)n >= sizeof(rel) ||
      !child(full, sizeof(full), t->root, rel)) return 0;
  struct STAT st;
  if (STAT(full, &st)) return 0;
  if ((st.st_mode & S_IFMT) == S_IFDIR)
    return add(&t->dirs, &t->nd, &t->dc, rel, 0);
  if ((st.st_mode & S_IFMT) != S_IFREG || st.st_size < 0) return 0;
  int ok = add(&t->files, &t->nf, &t->fc, rel, (uint64_t)st.st_size);
  if (ok && progress_callback && !(t->nf % 1024))
    progress_callback(t->nf, progress_context);
  return ok;
}

/* Switch directory IPC already includes file sizes. Read 32 entries per
 * request instead of stat'ing every file on a slow SD card. */
static int list_game(const char *dir, Tree *t) {
#ifdef __SWITCH__
  FsFileSystem *sd=fsdevGetDeviceFileSystem("sdmc");
  if(sd && !strncmp(dir,"sdmc:",5)) {
    FsDir d;
    Result rc=fsFsOpenDirectory(sd,dir+5,3,&d);
    if(R_SUCCEEDED(rc)) {
      FsDirectoryEntry *batch=malloc(32*sizeof(*batch));
      if(!batch) {fsDirClose(&d);return 0;}
      int ok=1;
      for(;;) {
        s64 count=0;
        rc=fsDirRead(&d,&count,32,batch);
        if(R_FAILED(rc)) {ok=0;break;}
        if(count<=0) break;
        for(s64 i=0;i<count;i++) {
          if(cancel_callback && cancel_callback(cancel_context)) {ok=0;break;}
          const FsDirectoryEntry *e=&batch[i];
          if(!strcmp(e->name,".") || !strcmp(e->name,"..") ||
             !strcmp(e->name,".listing")) continue;
          if(strchr(e->name,':') || strchr(e->name,'\\')) {ok=0;break;}
          char rel[1024];
          int n=snprintf(rel,sizeof(rel),"%s%s%s",t->relative,
                         *t->relative?"/":"",e->name);
          if(n<=0 || (size_t)n>=sizeof(rel)) {ok=0;break;}
          if(e->type==FsDirEntryType_Dir) {
            if(!add(&t->dirs,&t->nd,&t->dc,rel,0)) {ok=0;break;}
          } else if(e->type==FsDirEntryType_File && e->file_size>=0) {
            if(!add(&t->files,&t->nf,&t->fc,rel,(uint64_t)e->file_size)) {
              ok=0;break;
            }
            if(progress_callback && !(t->nf%1024))
              progress_callback(t->nf,progress_context);
          } else {ok=0;break;}
        }
        if(!ok) break;
      }
      free(batch);
      fsDirClose(&d);
      return ok;
    }
  }
#endif
  return list(dir,tree_entry,t);
}

static int entry_compare(const void *a, const void *b) {
  return strcmp(((const Entry *)a)->path, ((const Entry *)b)->path);
}
static unsigned depth(const char *s) {
  unsigned n = !!*s;
  for (; *s; s++)
    n += *s == '/';
  return n;
}
static int dir_compare(const void *a, const void *b) {
  const Entry *x = a, *y = b;
  unsigned dx = depth(x->path), dy = depth(y->path);
  return dx < dy ? -1 : dx > dy ? 1 : strcmp(x->path, y->path);
}
static int pointer_compare(const void *a, const void *b) {
  return strcmp((*(Entry *const *)a)->path, (*(Entry *const *)b)->path);
}
static Entry *parent(Entry **index, uint32_t n, const char *path) {
  char s[1024];
  strcpy(s, path);
  char *slash = strrchr(s, '/');
  if (slash)
    *slash = 0;
  else
    s[0] = 0;
  Entry key = {.path = s};
  Entry *p = &key;
  Entry **found = bsearch(&p, index, n, sizeof(*index), pointer_compare);
  return found ? *found : NULL;
}
static uint32_t prime(uint32_t n) {
  if (n < 3)
    n = 3;
  n |= 1;
  for (;; n += 2) {
    uint32_t d;
    for (d = 3; (uint64_t)d * d <= n; d += 2)
      if (n % d == 0)
        break;
    if ((uint64_t)d * d > n)
      return n;
  }
}
static const char *leaf(const char *s) {
  const char *p = strrchr(s, '/');
  return p ? p + 1 : s;
}
static uint32_t name_hash(uint32_t parent, const char *s) {
  uint32_t h = parent ^ 123456789;
  for (; *s; s++) {
    h = (h >> 5) | (h << 27);
    h ^= (unsigned char)*s;
  }
  return h;
}

/* The .part file stores completed directories, not copied game content.
 * Each record is checksummed, so an interrupted write can be ignored. */
typedef struct {
  char magic[8];
  uint64_t root_hash;
  int64_t timestamp;
} PartHeader;
typedef struct {
  uint32_t bytes;
  uint32_t checksum;
} PartRecord;
static uint32_t part_hash(const void *v, size_t n) {
  const unsigned char *p = v;
  uint32_t h = 2166136261u;
  for (size_t i=0; i<n; i++) h=(h^p[i])*16777619u;
  return h;
}
static uint64_t part_root_hash(const char *s) {
  uint64_t h=UINT64_C(14695981039346656037);
  while (*s) h=(h^(unsigned char)*s++)*UINT64_C(1099511628211);
  return h;
}
static int part_path(const char *root,int layered,char out[160]) {
  int n=snprintf(out,160,
    "sdmc:/switch/BackupLoaderMKW/cache/%016llx.part",
    (unsigned long long)(part_root_hash(root)^
                        (layered?UINT64_C(0x554e494f4e303031):0)));
  return n>0 && n<160;
}
int nx2_folder_part_clear_checked(const char *root) {
  char name[160];
  int ok=1;
  if(!root||!*root)return 0;
  for(int layered=0;layered<2;layered++){
    if(!part_path(root,layered,name)){ok=0;continue;}
    if(remove(name)&&errno!=ENOENT)ok=0;
  }
  return ok;
}
void nx2_folder_part_clear(const char *root){(void)nx2_folder_part_clear_checked(root);}
static void part_dirs(void) {
  mkdir("sdmc:/switch",0777);
  mkdir("sdmc:/switch/BackupLoaderMKW",0777);
  mkdir("sdmc:/switch/BackupLoaderMKW/cache",0777);
}
static int part_add_record(Tree *t, const unsigned char *p, uint32_t len) {
  if (len<6) return 0;
  uint16_t dl; memcpy(&dl,p,2);p+=2;len-=2;
  if (dl>=1024 || dl>len-4) return 0;
  char dir[1024];memcpy(dir,p,dl);dir[dl]=0;p+=dl;len-=dl;
  uint32_t count;memcpy(&count,p,4);p+=4;len-=4;
  /* Every child needs at least its 11-byte header and one name byte. */
  if (count>len/12) return 0;
  uint32_t id=t->nd;
  for(uint32_t j=0;j<t->nd;j++)
    if (!strcmp(t->dirs[j].path,dir)) {id=j;break;}
  if(id==t->nd) return 0;
  for(uint32_t i=0;i<count;i++) {
    if(len<11) return 0;
    unsigned type=*p++;len--;
    uint16_t nl;memcpy(&nl,p,2);p+=2;len-=2;
    uint64_t size;memcpy(&size,p,8);p+=8;len-=8;
    if(!nl || nl>=1024 || nl>len || (type!=1&&type!=2)) return 0;
    char rel[1024];memcpy(rel,p,nl);rel[nl]=0;p+=nl;len-=nl;
    size_t dl2=strlen(dir);
    if((dl2 && (strncmp(rel,dir,dl2)||rel[dl2]!='/')) ||
       strchr(rel,':') || strchr(rel,'\\')) return 0;
    if(type==1) {
      if(!add(&t->dirs,&t->nd,&t->dc,rel,0)) return 0;
    } else if(!add(&t->files,&t->nf,&t->fc,rel,size)) return 0;
  }
  if(len) return 0;
  t->dirs[id].hash=1;
  return 1;
}
static FILE *part_open(Tree *t, const char *root) {
  char name[160];
  if(!part_path(root,t->layered,name)) return NULL;
  struct STAT st;
  if(STAT(root,&st)) return NULL;
  PartHeader wanted={{'N','X','2','P','A','R','T','1'},
                     part_root_hash(root),(int64_t)st.st_mtime};
  if(t->layered)wanted.magic[7]='2';
  part_dirs();
  FILE *f=fopen(name,"rb+");
  if(f) {
    PartHeader h;
    if(fread(&h,1,sizeof(h),f)==sizeof(h) &&
       !memcmp(&h,&wanted,sizeof(h))) {
      long valid=ftell(f);
      for(;;) {
        PartRecord rec;
        if(fread(&rec,1,sizeof(rec),f)!=sizeof(rec)) break;
        if(!rec.bytes || rec.bytes>16*1024*1024) break;
        unsigned char *p=malloc(rec.bytes);
        if(!p) break;
        int ok=fread(p,1,rec.bytes,f)==rec.bytes &&
               part_hash(p,rec.bytes)==rec.checksum &&
               part_add_record(t,p,rec.bytes);
        free(p);
        if(!ok) break;
        valid=ftell(f);
      }
#ifndef _WIN32
      if(ftruncate(fileno(f),valid)) {fclose(f);return NULL;}
#endif
      fseek(f,valid,SEEK_SET);
      resume_files+=t->nf;
      if(progress_callback && t->nf) progress_callback(t->nf,progress_context);
      return f;
    }
    fclose(f);
  }
  f=fopen(name,"wb+");
  if(!f) return NULL;
  if(fwrite(&wanted,1,sizeof(wanted),f)!=sizeof(wanted) || fflush(f)) {
    fclose(f);return NULL;
  }
  return f;
}
static int part_write(FILE *f, const Tree *t, const char *dir,
                      uint32_t file_from, uint32_t dir_from) {
  if(!f) return 1; /* Indexing still works when checkpoint storage is absent. */
  size_t dl=strlen(dir);
  if(dl>UINT16_MAX) return 0;
  size_t bytes=2+dl+4;
  for(uint32_t i=file_from;i<t->nf;i++) bytes+=11+strlen(t->files[i].path);
  for(uint32_t i=dir_from;i<t->nd;i++) bytes+=11+strlen(t->dirs[i].path);
  if(bytes>16*1024*1024) return 0;
  unsigned char *p=malloc(bytes);
  if(!p) return 0;
  unsigned char *at=p;
  uint16_t d=(uint16_t)dl;memcpy(at,&d,2);at+=2;
  memcpy(at,dir,dl);at+=dl;
  uint32_t count=(t->nf-file_from)+(t->nd-dir_from);
  memcpy(at,&count,4);at+=4;
  for(unsigned kind=0;kind<2;kind++) {
    uint32_t first=kind?dir_from:file_from;
    uint32_t end=kind?t->nd:t->nf;
    for(uint32_t i=first;i<end;i++) {
      const Entry *e=kind?&t->dirs[i]:&t->files[i];
      size_t n=strlen(e->path);
      uint16_t l=(uint16_t)n;
      *at++=kind?1:2;
      memcpy(at,&l,2);at+=2;
      uint64_t size=kind?0:e->size;
      memcpy(at,&size,8);at+=8;
      memcpy(at,e->path,n);at+=n;
    }
  }
  PartRecord rec={(uint32_t)bytes,part_hash(p,bytes)};
  int ok=fwrite(&rec,1,sizeof(rec),f)==sizeof(rec) &&
         fwrite(p,1,bytes,f)==bytes && !fflush(f);
  free(p);
  return ok;
}

static int nup_entry(void *ctx,const char *path,int directory,uint64_t size) {
  Tree *t=ctx;
  if(cancel_callback && cancel_callback(cancel_context))return 0;
  int ok=directory?add(&t->dirs,&t->nd,&t->dc,path,0):
                   add(&t->files,&t->nf,&t->fc,path,size);
  if(ok && !directory && progress_callback && !(t->nf%1024))
    progress_callback(t->nf,progress_context);
  return ok;
}
static void tree_free(Tree *t){
  for(uint32_t i=0;i<t->nf;i++)free(t->files[i].path);
  for(uint32_t i=0;i<t->nd;i++)free(t->dirs[i].path);
  free(t->files);free(t->dirs);
}
static int tree_scan(Tree *t,int nup){
  if(!add(&t->dirs,&t->nd,&t->dc,"",0))return 0;
  FILE *part=nup?NULL:part_open(t,t->root);
  int ok=nup?nx2_nup_romfs_walk(nup_entry,t):1;
  for(uint32_t d=0;!nup && d<t->nd;d++){
    if(t->dirs[d].hash)continue;
    char dir[2400],rel[1024];
    snprintf(rel,sizeof(rel),"%s",t->dirs[d].path);
    snprintf(t->relative,sizeof(t->relative),"%s",rel);
    if(!*rel)snprintf(dir,sizeof(dir),"%s",t->root);
    else if(!child(dir,sizeof(dir),t->root,rel)){ok=0;break;}
    uint32_t file_from=t->nf,dir_from=t->nd;
    if(!list_game(dir,t) || !part_write(part,t,rel,file_from,dir_from)){
      ok=0;break;
    }
    t->dirs[d].hash=1;
  }
  if(part)fclose(part);
  return ok;
}
static int tree_has(Entry *entries,uint32_t count,const char *path){
  Entry key={.path=(char *)path};
  return count && bsearch(&key,entries,count,sizeof(*entries),entry_compare)!=NULL;
}
static int shadowed_by_file(Entry *files,uint32_t count,const char *path){
  char prefix[1024];
  snprintf(prefix,sizeof(prefix),"%s",path);
  for(char *p=prefix;*p;p++)if(*p=='/'){
    *p=0;int found=tree_has(files,count,prefix);*p='/';
    if(found)return 1;
  }
  return tree_has(files,count,path);
}
static int tree_union(Tree *update,const Tree *base){
  qsort(update->files,update->nf,sizeof(Entry),entry_compare);
  qsort(update->dirs,update->nd,sizeof(Entry),entry_compare);
  /* Appends preserve these sorted prefixes even when arrays reallocate. */
  uint32_t files=update->nf,dirs=update->nd;
  for(uint32_t i=0;i<base->nd;i++){
    const char *p=base->dirs[i].path;
    if(cancel_callback && cancel_callback(cancel_context))return 0;
    if(shadowed_by_file(update->files,files,p) ||
       tree_has(update->dirs,dirs,p))continue;
    if(!add(&update->dirs,&update->nd,&update->dc,p,0))return 0;
  }
  for(uint32_t i=0;i<base->nf;i++){
    const Entry *e=&base->files[i];
    if(cancel_callback && cancel_callback(cancel_context))return 0;
    if(shadowed_by_file(update->files,files,e->path) ||
       tree_has(update->dirs,dirs,e->path))continue;
    if(!add(&update->files,&update->nf,&update->fc,e->path,e->size))return 0;
    update->files[update->nf-1].source=1;
    if(progress_callback && !(update->nf%1024))
      progress_callback(update->nf,progress_context);
  }
  return 1;
}
void *nx2_folder_rom(const char *root,size_t *out_size,char error[160]){
  return nx2_folder_rom_layered(root,NULL,out_size,error);
}
void *nx2_folder_rom_layered(const char *root,const char *base_root,
                             size_t *out_size,char error[160]) {
  resume_files=0;
  Tree t = {.root = root};
  Tree base_tree={.root=base_root};
  Entry **index = NULL;
  unsigned char *blob = NULL;
  uint32_t *dh = NULL, *fh = NULL;
  *out_size = 0;
  snprintf(error, 160, "Could not scan or allocate the RomFS directory index");
  int nup=nx2_nup_active();
  int layered=base_root && *base_root && !nup;
  /* Old single-root checkpoints may have been made during an incomplete
   * transfer. Layered scans get fresh identities, then resume normally. */
  t.layered=base_tree.layered=layered;
  int scan_ok=tree_scan(&t,nup);
  if(scan_ok && layered)
    scan_ok=tree_scan(&base_tree,0) && tree_union(&t,&base_tree);
  if(!scan_ok || !t.nf) goto done;
  qsort(t.files, t.nf, sizeof(Entry), entry_compare);
  qsort(t.dirs, t.nd, sizeof(Entry), dir_compare);
  index = malloc((size_t)t.nd * sizeof(*index));
  if (!index)
    goto done;
  for (uint32_t i = 0; i < t.nd; i++)
    index[i] = &t.dirs[i];
  qsort(index, t.nd, sizeof(*index), pointer_compare);
  uint64_t dm = 0, fm = 0, strings = 0, data = 0;
  for (uint32_t i = 0; i < t.nd; i++) {
    t.dirs[i].at = (uint32_t)dm;
    dm = align64(dm + 24 + strlen(leaf(t.dirs[i].path)), 4);
  }
  for (uint32_t i = 0; i < t.nf; i++) {
    Entry *e = &t.files[i];
    e->at = (uint32_t)fm;
    fm = align64(fm + 32 + strlen(leaf(e->path)), 4);
    e->po = (uint32_t)strings;
    strings += strlen(e->path) + 1;
    data = align64(data, 16);
    e->data = data;
    if (e->size > INT64_MAX - data)
      goto done;
    data += e->size;
  }
  if (dm > UINT32_MAX || fm > UINT32_MAX || strings > UINT32_MAX)
    goto done;
  uint32_t db = prime(t.nd), fb = prime(t.nf);
  uint64_t dho = 80, dmo = dho + db * 4ULL, fho = dmo + dm,
           fmo = fho + fb * 4ULL, ms = fmo + fm, base = align64(ms, 4096);
  uint64_t so = 80 + t.nf * 32ULL, mo = align64(so + strings, 16),
           bytes = mo + ms;
  if (bytes > 128 * 1024 * 1024 || base > INT64_MAX - data - 4095)
    goto done;
  blob = calloc(1, (size_t)bytes);
  dh = malloc(db * 4ULL);
  fh = malloc(fb * 4ULL);
  if (!blob || !dh || !fh) {
    free(blob);
    blob = NULL;
    goto done;
  }
  memset(dh, 255, db * 4ULL);
  memset(fh, 255, fb * 4ULL);
  for (uint32_t i = t.nd; i > 1; i--) {
    Entry *e = &t.dirs[i - 1], *p = parent(index, t.nd, e->path);
    if (!p) {
      free(blob);
      blob = NULL;
      goto done;
    }
    e->parent = p->at;
    e->next = p->dir;
    p->dir = e->at;
  }
  for (uint32_t i = t.nf; i; i--) {
    Entry *e = &t.files[i - 1], *p = parent(index, t.nd, e->path);
    if (!p) {
      free(blob);
      blob = NULL;
      goto done;
    }
    e->parent = p->at;
    e->next = p->file;
    p->file = e->at;
  }
  for (uint32_t i = 0; i < t.nd; i++) {
    Entry *e = &t.dirs[i];
    const char *name = leaf(e->path);
    uint32_t bucket = name_hash(e->parent, name) % db;
    unsigned char *p = blob + mo + dmo + e->at;
    p32(p, e->parent);
    p32(p + 4, e->next);
    p32(p + 8, e->dir);
    p32(p + 12, e->file);
    p32(p + 16, dh[bucket]);
    p32(p + 20, (uint32_t)strlen(name));
    memcpy(p + 24, name, strlen(name));
    dh[bucket] = e->at;
  }
  for (uint32_t i = 0; i < t.nf; i++) {
    Entry *e = &t.files[i];
    const char *name = leaf(e->path);
    uint32_t bucket = name_hash(e->parent, name) % fb;
    unsigned char *p = blob + mo + fmo + e->at;
    p32(p, e->parent);
    p32(p + 4, e->next);
    p64(p + 8, e->data);
    p64(p + 16, e->size);
    p32(p + 24, fh[bucket]);
    p32(p + 28, (uint32_t)strlen(name));
    memcpy(p + 32, name, strlen(name));
    fh[bucket] = e->at;
    p = blob + 80 + i * 32ULL;
    p64(p, base + e->data);
    p64(p + 8, e->size);
    p32(p + 16, e->po);
    p32(p + 20, (uint32_t)strlen(e->path));
    p64(p + 24, e->source);
    memcpy(blob + so + e->po, e->path, strlen(e->path));
  }
  for (uint32_t i = 0; i < db; i++)
    p32(blob + mo + dho + i * 4ULL, dh[i]);
  for (uint32_t i = 0; i < fb; i++)
    p32(blob + mo + fho + i * 4ULL, fh[i]);
  uint64_t meta[] = {80,  dho,       db * 4ULL, dmo, dm,
                     fho, fb * 4ULL, fmo,       fm,  base};
  for (unsigned i = 0; i < 10; i++)
    p64(blob + mo + i * 8, meta[i]);
  memcpy(blob, "NX2ROM01", 8);
  p32(blob + 8, layered?2:1);
  p32(blob + 12, 80);
  p64(blob + 16, ms);
  p64(blob + 24, align64(base + data, 4096));
  p32(blob + 32, t.nf);
  p32(blob + 36, 32);
  p64(blob + 40, 80);
  p64(blob + 48, so);
  p64(blob + 56, strings);
  p64(blob + 64, mo);
  p64(blob + 72, base);
  *out_size = (size_t)bytes;
  error[0] = 0;
done:
  tree_free(&t);tree_free(&base_tree);
  free(index);
  free(dh);
  free(fh);
  return blob;
}
