/* SPDX-License-Identifier: GPL-2.0-only */
#include "nup.h"
#include <limits.h>
#ifdef _WIN32
#define nup_seek _fseeki64
#define nup_tell _ftelli64
#else
#define nup_seek fseeko
#define nup_tell ftello
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __SWITCH__
#include <switch.h>
#endif

static uint32_t rd32(const unsigned char *p) {
  return (uint32_t)p[0] | (uint32_t)p[1]<<8 |
         (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint64_t rd64(const unsigned char *p) {
  return rd32(p) | (uint64_t)rd32(p+4)<<32;
}
static unsigned rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32-n)); }
static void sha256(const unsigned char *data, size_t size, unsigned char out[32]) {
  static const uint32_t k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
  };
  uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                   0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  uint64_t bitlen = (uint64_t)size * 8;
  size_t blocks = size/64 + 1 + (size%64 >= 56);
  for (size_t b=0; b<blocks; b++) {
    unsigned char block[64] = {0};
    size_t off=b*64;
    if (off<size) {
      size_t n=size-off;
      if (n>64) n=64;
      memcpy(block,data+off,n);
    }
    if (off<=size && size-off<64) block[size-off]=0x80;
    if (b==blocks-1)
      for (unsigned i=0;i<8;i++) block[56+i]=(unsigned char)(bitlen>>(56-8*i));
    uint32_t w[64];
    for (unsigned i=0;i<16;i++) w[i]=(uint32_t)block[i*4]<<24 |
      (uint32_t)block[i*4+1]<<16 | (uint32_t)block[i*4+2]<<8 | block[i*4+3];
    for (unsigned i=16;i<64;i++) {
      uint32_t s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);
      uint32_t s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);
      w[i]=w[i-16]+s0+w[i-7]+s1;
    }
    uint32_t a=h[0],bb=h[1],cc=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for (unsigned i=0;i<64;i++) {
      uint32_t s1=rotr(e,6)^rotr(e,11)^rotr(e,25);
      uint32_t ch=(e&f)^(~e&g);
      uint32_t t1=hh+s1+ch+k[i]+w[i];
      uint32_t s0=rotr(a,2)^rotr(a,13)^rotr(a,22);
      uint32_t maj=(a&bb)^(a&cc)^(bb&cc);
      uint32_t t2=s0+maj;
      hh=g;g=f;f=e;e=d+t1;d=cc;cc=bb;bb=a;a=t1+t2;
    }
    h[0]+=a;h[1]+=bb;h[2]+=cc;h[3]+=d;
    h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
  }
  for (unsigned i=0;i<8;i++) for(unsigned j=0;j<4;j++)
    out[4*i+j]=(unsigned char)(h[i]>>(24-8*j));
}
/* Select one complete package, preferring a later full update.
 * This is selection, not a synthetic patch overlay. */
static int nup_select_runnable(const unsigned char *toc,uint32_t packages,
                               uint32_t *selected,int *updated){
  uint32_t base=UINT32_MAX,update=UINT32_MAX,standalone=UINT32_MAX;
  for(uint32_t i=0;i<packages;i++){
    const unsigned char *p=toc+48+(size_t)i*64;
    if(rd32(p+12)==UINT32_MAX||rd32(p+16)==UINT32_MAX)continue;
    if(p[24]==0 && base==UINT32_MAX)base=i;
    else if(p[24]==1){
      if(p[25]&1)standalone=i;
      else update=i;
    }
  }
  if(update!=UINT32_MAX){*selected=update;*updated=1;return 1;}
  if(base!=UINT32_MAX){*selected=base;*updated=0;return 1;}
  if(standalone!=UINT32_MAX){*selected=standalone;*updated=1;return 1;}
  return 0;
}
static const char *probe_error = "Not checked";
static char probe_details[160];
const char *nx2_nup_probe_error(void) { return probe_error; }
int nx2_nup_probe(const char *path, uint64_t *title_id, uint64_t *files) {
  unsigned char header[128], digest[32];
  FILE *f=fopen(path,"rb");
  probe_details[0]=0;
  probe_error = "Cannot open NUP file";
  if (!f) return 0;
  probe_error = "Incomplete NUP header";
  int ok=0;
  unsigned char *toc=NULL;
  if (fread(header,1,sizeof(header),f)!=sizeof(header)) goto done;
  probe_error = "Invalid NUP v1 header";
  if (memcmp(header,"NUP\1",4) || header[4]!=0x80 || header[5] ||
      header[6]!=1 || header[7] || rd32(header+0x4c) ||
      rd64(header+8)!=128) goto done;
  uint64_t toc_size=rd64(header+0x10), blob=rd64(header+0x38),
           file_size=rd64(header+0x40);
  uint32_t packages=rd32(header+0x48);
  probe_error = "Incomplete copy, invalid file size or TOC bounds";
  if (!packages || packages>4096 || toc_size<48 || toc_size>128*1024*1024 ||
      blob%16 || blob<128 || toc_size>blob-128 || file_size<blob ||
      nup_seek(f,0,SEEK_END) || nup_tell(f)<0 || (uint64_t)nup_tell(f)!=file_size ||
      nup_seek(f,128,SEEK_SET)) goto done;
  probe_error = "Cannot read NUP table of contents";
  toc=malloc((size_t)toc_size);
  if (!toc || fread(toc,1,(size_t)toc_size,f)!=(size_t)toc_size) goto done;
  probe_error = "NUP table checksum or format mismatch";
  sha256(toc,(size_t)toc_size,digest);
  if (memcmp(digest,header+0x18,32) || memcmp(toc,"TOC\1",4) ||
      rd32(toc+4)!=packages || rd64(toc+0x28) ||
      toc_size<48+(uint64_t)packages*64) goto done;
  uint64_t dirs=rd64(toc+8), nfiles=rd64(toc+16);
  uint64_t pool_off=rd64(toc+0x18), pool_len=rd64(toc+0x20);
  uint64_t start=48+(uint64_t)packages*64;
  probe_error = "NUP directory/file counts exceed supported limits";
  if (!dirs || dirs>262144 || nfiles>262144 ||
      dirs>(toc_size-start)/24) goto done;
  start+=dirs*24;
  if (nfiles>(toc_size-start)/80) goto done;
  start+=nfiles*80;
  if (pool_off<start || pool_off>toc_size || pool_len>toc_size-pool_off) goto done;
  probe_error = "NUP has no base-game package";
  for (uint32_t i=0;i<packages;i++) {
    const unsigned char *p=toc+48+(size_t)i*64;
    uint64_t id=rd64(p);
    uint32_t root=rd32(p+8),exefs=rd32(p+12),romfs=rd32(p+16);
    unsigned kind=p[24],flags=p[25];
    if (kind>=5 || (flags&~1) || p[26] || p[27] || rd32(p+28) ||
        root>=dirs || (exefs!=UINT32_MAX && exefs>=dirs) ||
        (romfs!=UINT32_MAX && romfs>=dirs)) {
      snprintf(probe_details,sizeof(probe_details),
        "Pkg %u invalid: kind=%u flags=%u root=%u exefs=%u romfs=%u",
        i,kind,flags,root,exefs,romfs);
      probe_error=probe_details;
      goto done;
    }
    /* Do not reject a partial base here: a later complete update can
     * supply an executable RomFS view. Selection happens after validation. */
    (void)id;
  }
  uint32_t selected=UINT32_MAX;
  int selected_update=0;
  if(nup_select_runnable(toc,packages,&selected,&selected_update)){
    const unsigned char *p=toc+48+(size_t)selected*64;
    /* Preserve the base application identity when an update is selected. */
    uint64_t id=rd64(p);
    if(selected_update)for(uint32_t i=0;i<packages;i++){
      const unsigned char *base=toc+48+(size_t)i*64;
      if(base[24]==0){id=rd64(base);break;}
    }
    if(title_id)*title_id=id;
    if(files)*files=nfiles;
    ok=1;
    snprintf(probe_details,sizeof(probe_details),
      "Ready: %s pkg %u (complete ExeFS+RomFS)",
      selected_update?"update":"base",selected);
    probe_error=probe_details;
  }else{
    probe_error="No complete runnable base/update ExeFS+RomFS package";
  }
done:
  free(toc);
  fclose(f);
  return ok;
}

/* Base-package contents are a virtual filesystem. Read a file by name and
 * offset rather than unpacking its siblings onto the SD card. */
typedef struct {
  FILE *f;
#ifdef __SWITCH__
  FsFile native_file;
  int native_open;
#endif
  unsigned char *toc;
  size_t toc_size;
  uint64_t blob_start, file_length;
  uint64_t ndirs, nfiles;
  uint32_t exefs_dir, romfs_dir;
  uint64_t meta_offset, meta_size;
  const unsigned char *dirs, *files, *children, *pool;
  uint64_t child_count, pool_size;
  /* Two-entry LRU avoids repeatedly decompressing alternating Zstd assets.
   * Bounded at 32 MiB: the existing single entry could already reach 16 MiB. */
  uint32_t cached_file[2];
  unsigned char *cached_bytes[2];
  size_t cached_size[2];
  uint64_t cached_stamp[2],cache_clock;
} NupMount;
static NupMount mounted;

/* Standalone Zstandard decoder, vendored from its dual BSD/GPL source. */
extern size_t ZSTD_decompress(void *,size_t,const void *,size_t);
extern unsigned ZSTD_isError(size_t);
typedef struct ZSTD_DCtx_s ZSTD_DStream;
typedef struct {const void *src;size_t size,pos;} NupIn;
typedef struct {void *dst;size_t size,pos;} NupOut;
extern ZSTD_DStream *ZSTD_createDStream(void);
extern size_t ZSTD_initDStream(ZSTD_DStream *);
extern size_t ZSTD_decompressStream(ZSTD_DStream *,NupOut *,NupIn *);
extern size_t ZSTD_freeDStream(ZSTD_DStream *);

/* NUP v1 has no frame seek table. Keep two bounded decoder cursors for
 * large Zstd entries across consecutive reads; host RPCs are serialized. */
typedef struct {
  uint32_t file;
  ZSTD_DStream *ctx;
  unsigned char *input,*output;
  uint64_t at,packed,total,consumed,produced,last_output_at,stamp;
  size_t input_size,input_pos,last_output_size;
  int finished;
} NupLargeStream;
static NupLargeStream large_stream[2];
static uint64_t large_clock;
static void large_discard(NupLargeStream *s) {
  if(s->ctx)ZSTD_freeDStream(s->ctx);
  free(s->input);free(s->output);
  memset(s,0,sizeof(*s));
  s->file=UINT32_MAX;
}
static uint32_t dirchild(const NupMount *m,uint32_t d,uint32_t j) {
  return rd32(m->children+(rd32(m->dirs+(size_t)d*24+8)+j)*4);
}
static const unsigned char *name_for(const NupMount *m,uint32_t node,
                                    uint32_t *length) {
  const unsigned char *r;
  if(node<m->ndirs) r=m->dirs+(size_t)node*24;
  else r=m->files+(size_t)(node-m->ndirs)*80;
  uint32_t off=rd32(r),len=rd32(r+4);
  if(off>m->pool_size||len>m->pool_size-off) return NULL;
  if(length) *length=len;
  return m->pool+off;
}
static int safe_name(const unsigned char *s,uint32_t n) {
  if(!s||!n||n>255||(n==1&&s[0]=='.')||
     (n==2&&s[0]=='.'&&s[1]=='.'))return 0;
  for(uint32_t i=0;i<n;i++)
    if(!s[i]||s[i]=='/'||s[i]=='\\'||s[i]==':')return 0;
  return 1;
}
static uint32_t lookup_child(NupMount *m,uint32_t dir,
                             const char *name,size_t len) {
  if(dir>=m->ndirs)return UINT32_MAX;
  const unsigned char *d=m->dirs+(size_t)dir*24;
  uint32_t start=rd32(d+8),count=rd32(d+12);
  if(start>m->child_count||count>m->child_count-start)
    return UINT32_MAX;
  /* Names are sorted in the NUP TOC. A linear fallback also works if
   * the producer hasn't preserved bytewise ordering. */
  for(uint32_t i=0;i<count;i++) {
    uint32_t node=dirchild(m,dir,i),n=0;
    const unsigned char *s=name_for(m,node,&n);
    if(s&&n==len&&!memcmp(s,name,len))return node;
  }
  return UINT32_MAX;
}
static uint32_t lookup_file(const char *path) {
  if(!mounted.f||!path)return UINT32_MAX;
  uint32_t dir;
  if(!strncmp(path,"romfs/",6)) {
    dir=mounted.romfs_dir;path+=6;
  } else if(!strncmp(path,"exefs/",6)) {
    dir=mounted.exefs_dir;path+=6;
  } else return UINT32_MAX;
  if(dir==UINT32_MAX)return UINT32_MAX;
  while(*path) {
    const char *slash=strchr(path,'/');
    size_t len=slash?(size_t)(slash-path):strlen(path);
    if(!len||len>255)return UINT32_MAX;
    uint32_t node=lookup_child(&mounted,dir,path,len);
    if(node==UINT32_MAX)return UINT32_MAX;
    if(!slash)return node>=mounted.ndirs?node-mounted.ndirs:UINT32_MAX;
    if(node>=mounted.ndirs)return UINT32_MAX;
    dir=node;path=slash+1;
  }
  return UINT32_MAX;
}
void nx2_nup_close(void) {
#ifdef __SWITCH__
  if(mounted.native_open)fsFileClose(&mounted.native_file);
#endif
  if(mounted.f)fclose(mounted.f);
  free(mounted.toc);
  for(unsigned j=0;j<2;j++){
    free(mounted.cached_bytes[j]);
    large_discard(&large_stream[j]);
  }
  large_clock=0;
  memset(&mounted,0,sizeof(mounted));
}
int nx2_nup_active(void) {return mounted.f!=NULL;}
int nx2_nup_open(const char *path) {
  nx2_nup_close();
  probe_error="NUP mount not started";
  /* One TOC read and checksum per launch. Catalog discovery already
   * performed the full probe; repeat validation here against the data that
   * this mount will actually use without rereading the TOC twice. */
  probe_error="NUP reopen failed";
  FILE *f=fopen(path,"rb");
  if(!f)return 0;
  probe_error="NUP header read failed";
  unsigned char h[128];
  if(fread(h,1,128,f)!=128){fclose(f);return 0;}
  probe_error="NUP TOC too large or invalid";
  uint64_t count=rd64(h+0x10),nd=0,nf=0;
  uint32_t declared=rd32(h+0x48);
  if(memcmp(h,"NUP\1",4) || h[4]!=0x80 || h[5] ||
     h[6]!=1 || h[7] || rd64(h+8)!=128 ||
     !declared || declared>4096 || count>128*1024*1024 ||
     count<48+(uint64_t)declared*64) {
    fclose(f);return 0;
  }
  probe_error="NUP TOC allocation or read failed";
  unsigned char *toc=malloc((size_t)count);
  if(!toc||fread(toc,1,(size_t)count,f)!=(size_t)count) {
    free(toc);fclose(f);return 0;
  }
  unsigned char digest[32];
  sha256(toc,(size_t)count,digest);
  if(memcmp(digest,h+0x18,32)||memcmp(toc,"TOC\1",4) ||
     rd32(toc+4)!=declared) {
    probe_error="NUP TOC checksum or package count mismatch";
    free(toc);fclose(f);return 0;
  }
  uint32_t pc=rd32(toc+4);
  nd=rd64(toc+8);nf=rd64(toc+16);
  uint64_t pool_off=rd64(toc+24),pool_len=rd64(toc+32);
  uint64_t dirs_start=48+(uint64_t)pc*64;
  uint64_t files_start=dirs_start+nd*24,children_start=files_start+nf*80;
  probe_error="NUP TOC node or string-pool layout invalid";
  if(!nd||nd>262144||nf>262144||children_start>pool_off||
     pool_off>count||pool_len>count-pool_off||
     (pool_off-children_start)%4) {
    free(toc);fclose(f);return 0;
  }
  NupMount m={0};
  m.f=f;m.toc=toc;m.toc_size=(size_t)count;
  m.blob_start=rd64(h+0x38);m.file_length=rd64(h+0x40);
  probe_error="NUP mount size or blob bounds changed since discovery";
  if(m.blob_start<128 || (m.blob_start&15) ||
     count>m.blob_start-128 || m.file_length<m.blob_start ||
     nup_seek(f,0,SEEK_END) || nup_tell(f)<0 ||
     (uint64_t)nup_tell(f)!=m.file_length)goto fail;
  m.ndirs=nd;m.nfiles=nf;
  m.dirs=toc+dirs_start;m.files=toc+files_start;
  m.children=toc+children_start;m.pool=toc+pool_off;
  m.child_count=(pool_off-children_start)/4;m.pool_size=pool_len;
  m.exefs_dir=m.romfs_dir=UINT32_MAX;
  for(unsigned j=0;j<2;j++)m.cached_file[j]=UINT32_MAX;
  for(unsigned j=0;j<2;j++)large_stream[j].file=UINT32_MAX;
  uint32_t selected=UINT32_MAX;
  int selected_update=0;
  probe_error="No complete runnable base/update ExeFS+RomFS package";
  if(!nup_select_runnable(toc,pc,&selected,&selected_update))goto fail;
  const unsigned char *chosen=toc+48+(size_t)selected*64;
  m.exefs_dir=rd32(chosen+12);
  m.romfs_dir=rd32(chosen+16);
  m.meta_offset=rd64(chosen+32);
  m.meta_size=rd32(chosen+40);
  probe_error="NUP executable or RomFS root invalid";
  if(m.exefs_dir>=nd||m.romfs_dir>=nd)goto fail;
  probe_error="NUP invalid directory child/metadata";
  /* Validate every directory edge and file extent before exposing paths. */
  for(uint64_t i=0;i<nd;i++){
    const unsigned char *d=m.dirs+i*24;
    uint32_t first=rd32(d+8),n=rd32(d+12);
    if(first>m.child_count||n>m.child_count-first||rd64(d+16))goto fail;
    for(uint32_t j=0;j<n;j++){
      uint32_t child=dirchild(&m,(uint32_t)i,j),len=0;
      const unsigned char *nm=(uint64_t)child<nd+nf?name_for(&m,child,&len):NULL;
      if(!safe_name(nm,len))goto fail;
    }
  }
  probe_error="NUP invalid file name or compressed extent";
  for(uint64_t i=0;i<nf;i++){
    const unsigned char *e=m.files+i*80;
    uint32_t nl=0;
    const unsigned char *nm=name_for(&m,(uint32_t)(nd+i),&nl);
    if(!safe_name(nm,nl))goto fail;
    uint64_t size=rd64(e+8),at=rd64(e+16),stored=rd64(e+24);
    unsigned codec=e[64];
    if(codec>1||e[65]||e[66]||e[67]||
       (size==0 && (at||stored||codec))||
       (size>0 && (at<m.blob_start||(at&15)||
                    at>m.file_length||stored>m.file_length-at))||
       (codec==0 && stored!=size))goto fail;
  }
#ifdef __SWITCH__
  /* Runtime reads bypass FILE seek state and newlib stream locking. The
   * archive stays owned by the single storage worker; TOC/NPDM metadata
   * preparation can still use the original stream before guest entry. */
  probe_error="NUP native SD file open failed";
  FsFileSystem *sd=fsdevGetDeviceFileSystem("sdmc");
  if(!sd || strncmp(path,"sdmc:/",6) ||
     R_FAILED(fsFsOpenFile(sd,path+5,FsOpenMode_Read,&m.native_file)))goto fail;
  m.native_open=1;
  s64 actual=0;
  Result native_rc=fsFileGetSize(&m.native_file,&actual);
  if(R_FAILED(native_rc) || actual<0 || (uint64_t)actual!=m.file_length){
    fsFileClose(&m.native_file);
    probe_error="NUP native file size changed during mount";
    goto fail;
  }
#endif
  mounted=m;
  snprintf(probe_details,sizeof(probe_details),
    "NUP mounted %s pkg %u (complete ExeFS+RomFS)",
    selected_update?"update":"base",selected);
  probe_error=probe_details;
  return 1;
fail:
  free(toc);
  fclose(f);
  return 0;
}
/* Metadata is outside the ExeFS file tree: package record 0x20/0x28
 * points to TLVs. Type 0 holds an independent NPDM copy. */
void *nx2_nup_package_npdm(size_t *length) {
  if(length)*length=0;
  if(!mounted.f || !mounted.meta_size || mounted.meta_size>0x100000 ||
     mounted.meta_offset>mounted.file_length ||
     mounted.meta_size>mounted.file_length-mounted.meta_offset)
    return NULL;
  unsigned char *meta=malloc((size_t)mounted.meta_size);
  if(!meta)return NULL;
  if(mounted.meta_offset>INT64_MAX ||
     nup_seek(mounted.f,(int64_t)mounted.meta_offset,SEEK_SET) ||
     fread(meta,1,(size_t)mounted.meta_size,mounted.f)!=(size_t)mounted.meta_size){
    free(meta);return NULL;
  }
  size_t pos=0;
  while(pos+8<=(size_t)mounted.meta_size){
    uint16_t type=(uint16_t)(meta[pos]|(meta[pos+1]<<8));
    uint16_t reserved=(uint16_t)(meta[pos+2]|(meta[pos+3]<<8));
    uint32_t n=rd32(meta+pos+4);pos+=8;
    if(reserved || n>mounted.meta_size-pos)break;
    if(type==0 && n>=128 && !memcmp(meta+pos,"META",4)){
      void *copy=malloc(n);
      if(copy){memcpy(copy,meta+pos,n);if(length)*length=n;}
      free(meta);return copy;
    }
    pos+=n;
  }
  free(meta);return NULL;
}
static const char *read_error="No archive read attempted";
static char io_error[160];
const char *nx2_nup_read_error(void){return read_error;}

/* The worker mutates and the launcher watchdog reads this without RPC.
 * A blocked backend call still leaves its last I/O phase observable. */
static Nx2NupReadProgress read_progress;
static void progress_begin(uint32_t file,uint32_t codec,uint64_t n){
 __atomic_store_n(&read_progress.active,0,__ATOMIC_RELEASE);
 __atomic_store_n(&read_progress.file,file,__ATOMIC_RELAXED);
 __atomic_store_n(&read_progress.codec,codec,__ATOMIC_RELAXED);
 __atomic_store_n(&read_progress.requested,n,__ATOMIC_RELAXED);
 __atomic_store_n(&read_progress.sd_bytes,0,__ATOMIC_RELAXED);
 __atomic_store_n(&read_progress.decoded_bytes,0,__ATOMIC_RELAXED);
 __atomic_store_n(&read_progress.delivered_bytes,0,__ATOMIC_RELAXED);
 __atomic_store_n(&read_progress.phase,1,__ATOMIC_RELAXED);
 __atomic_store_n(&read_progress.io_inflight,0,__ATOMIC_RELAXED);
 __atomic_store_n(&read_progress.io_result,0,__ATOMIC_RELAXED);
 __atomic_store_n(&read_progress.chunk_offset,0,__ATOMIC_RELAXED);
 __atomic_store_n(&read_progress.chunk_bytes,0,__ATOMIC_RELAXED);
 __atomic_store_n(&read_progress.active,1,__ATOMIC_RELEASE);
}
static void progress_phase(uint32_t phase){
 __atomic_store_n(&read_progress.phase,phase,__ATOMIC_RELEASE);
}
static void progress_done(void){
 __atomic_store_n(&read_progress.active,0,__ATOMIC_RELEASE);
 __atomic_store_n(&read_progress.phase,0,__ATOMIC_RELEASE);
}
void nx2_nup_get_progress(Nx2NupReadProgress *out){
 if(!out)return;
 out->active=__atomic_load_n(&read_progress.active,__ATOMIC_ACQUIRE);
 out->file=__atomic_load_n(&read_progress.file,__ATOMIC_RELAXED);
 out->codec=__atomic_load_n(&read_progress.codec,__ATOMIC_RELAXED);
 out->phase=__atomic_load_n(&read_progress.phase,__ATOMIC_ACQUIRE);
 out->requested=__atomic_load_n(&read_progress.requested,__ATOMIC_RELAXED);
 out->sd_bytes=__atomic_load_n(&read_progress.sd_bytes,__ATOMIC_RELAXED);
 out->decoded_bytes=__atomic_load_n(&read_progress.decoded_bytes,__ATOMIC_RELAXED);
 out->delivered_bytes=__atomic_load_n(&read_progress.delivered_bytes,__ATOMIC_RELAXED);
 out->chunk_offset=__atomic_load_n(&read_progress.chunk_offset,__ATOMIC_RELAXED);
 out->chunk_bytes=__atomic_load_n(&read_progress.chunk_bytes,__ATOMIC_RELAXED);
 out->io_result=__atomic_load_n(&read_progress.io_result,__ATOMIC_RELAXED);
 out->io_inflight=__atomic_load_n(&read_progress.io_inflight,__ATOMIC_ACQUIRE);
 out->native_io=__atomic_load_n(&read_progress.native_io,__ATOMIC_RELAXED);
}
/* Exact offset-based archive reads. A returned failure or short transfer
 * stays a failure; no retries of an in-flight call or synthetic asset bytes. */
static int read_archive(uint64_t offset,void *dst,size_t length){
 unsigned char *p=dst;
 size_t pos=0;
 if(offset>mounted.file_length || length>mounted.file_length-offset){
  read_error="NUP archive read exceeds mounted file bounds";return 0;
 }
#ifndef __SWITCH__
 if(offset>INT64_MAX || nup_seek(mounted.f,(int64_t)offset,SEEK_SET)){
  read_error="NUP archive seek failed";return 0;
 }
#endif
 progress_phase(2);
 while(pos<length){
  size_t want=length-pos;
  if(want>0x10000U)want=0x10000U;
  __atomic_store_n(&read_progress.chunk_offset,offset+pos,__ATOMIC_RELAXED);
  __atomic_store_n(&read_progress.chunk_bytes,(uint32_t)want,__ATOMIC_RELAXED);
  __atomic_store_n(&read_progress.io_result,UINT32_MAX,__ATOMIC_RELAXED);
  __atomic_store_n(&read_progress.io_inflight,1,__ATOMIC_RELEASE);
  uint64_t got=0;uint32_t rc=0;
#ifdef __SWITCH__
  __atomic_store_n(&read_progress.native_io,1,__ATOMIC_RELAXED);
  rc=fsFileRead(&mounted.native_file,offset+pos,p+pos,want,FsReadOption_None,&got);
#else
  got=fread(p+pos,1,want,mounted.f);
  if(ferror(mounted.f))rc=1;
#endif
  if(got)__atomic_fetch_add(&read_progress.sd_bytes,(uint64_t)got,__ATOMIC_RELAXED);
  __atomic_store_n(&read_progress.io_result,rc,__ATOMIC_RELAXED);
  __atomic_store_n(&read_progress.io_inflight,0,__ATOMIC_RELEASE);
  if(rc || got!=want){
   snprintf(io_error,sizeof(io_error),
            "NUP I/O result=%08x offset=%llu requested=%llu read=%llu",
            rc,(unsigned long long)(offset+pos),
            (unsigned long long)want,(unsigned long long)got);
   read_error=io_error;return 0;
  }
  pos+=got;
 }
 return 1;
}
int nx2_nup_entry_detail(const char *path,uint64_t *stored,uint64_t *offset){
  uint32_t i=lookup_file(path);
  if(i==UINT32_MAX)return 0;
  const unsigned char *e=mounted.files+(size_t)i*80;
  if(stored)*stored=rd64(e+24);
  if(offset)*offset=rd64(e+16);
  return 1;
}
int nx2_nup_file_info(const char *path,uint64_t *size,unsigned *codec){
  uint32_t i=lookup_file(path);
  if(i==UINT32_MAX)return 0;
  const unsigned char *e=mounted.files+(size_t)i*80;
  if(size)*size=rd64(e+8);
  if(codec)*codec=e[64];
  return 1;
}

static int large_begin(NupLargeStream *s,uint32_t file,
                       uint64_t at,uint64_t packed,uint64_t total) {
  large_discard(s);
  s->ctx=ZSTD_createDStream();
  s->input=malloc(128*1024);
  s->output=malloc(128*1024);
  if(!s->ctx||!s->input||!s->output ||
     ZSTD_isError(ZSTD_initDStream(s->ctx))) {
    read_error="Zstandard large-stream allocation/init failure";
    large_discard(s);
    return 0;
  }
  s->file=file;s->at=at;s->packed=packed;s->total=total;
  return 1;
}
static int read_large_compressed(const unsigned char *e,uint32_t file,
                                 uint64_t offset,void *dest,size_t length) {
  uint64_t total=rd64(e+8),at=rd64(e+16),packed=rd64(e+24);
  uint64_t wanted=offset+(uint64_t)length;
  NupLargeStream *s=NULL;
  for(unsigned i=0;i<2;i++) {
    NupLargeStream *candidate=&large_stream[i];
    if(candidate->file!=file || !candidate->ctx)continue;
    if(offset<candidate->produced &&
       offset<candidate->last_output_at) {
      /* Older data is outside the 128-KiB backtrack window. */
      large_discard(candidate);
    }else s=candidate;
    break;
  }
  if(!s) {
    unsigned slot=large_stream[0].file==UINT32_MAX?0:
                  large_stream[1].file==UINT32_MAX?1:
                  large_stream[0].stamp<=large_stream[1].stamp?0:1;
    s=&large_stream[slot];
    if(!large_begin(s,file,at,packed,total))return 0;
  }
  s->stamp=++large_clock;
  unsigned char *out=dest;
  uint64_t position=offset;
  size_t left=length;
  unsigned stalls=0;
  while(left) {
    uint64_t cached_end=s->last_output_at+s->last_output_size;
    if(s->last_output_size && position>=s->last_output_at &&
       position<cached_end) {
      uint64_t available=cached_end-position;
      size_t take=(size_t)(available<left?available:left);
      memcpy(out,s->output+(size_t)(position-s->last_output_at),take);
      out+=take;position+=take;left-=take;
      __atomic_fetch_add(&read_progress.delivered_bytes,(uint64_t)take,__ATOMIC_RELAXED);
      continue;
    }
    if(position<s->produced || s->finished) {
      read_error="Zstandard large-stream requested unavailable output";
      large_discard(s);
      return 0;
    }
    if(s->input_pos==s->input_size && s->consumed<s->packed) {
      size_t n=(size_t)(s->packed-s->consumed);
      if(n>128*1024)n=128*1024;
      if(s->consumed>UINT64_MAX-s->at){
        read_error="Zstandard large-stream packed offset overflow";
        large_discard(s);return 0;
      }
      if(!read_archive(s->at+s->consumed,s->input,n)) {
        large_discard(s);
        return 0;
      }
      s->consumed+=n;s->input_size=n;s->input_pos=0;
    }
    NupIn in={s->input,s->input_size,s->input_pos};
    NupOut z={s->output,128*1024,0};
    size_t prior=in.pos;
    progress_phase(3);
    size_t step=ZSTD_decompressStream(s->ctx,&z,&in);
    s->input_pos=in.pos;
    if(ZSTD_isError(step)) {
      snprintf(probe_details,sizeof(probe_details),
          "ZSTD stream code=%llu packed=%llu/%llu produced=%llu/%llu",
          (unsigned long long)(0-(uint64_t)step),
          (unsigned long long)s->consumed,(unsigned long long)s->packed,
          (unsigned long long)s->produced,(unsigned long long)s->total);
      read_error=probe_details;
      large_discard(s);
      return 0;
    }
    if(z.pos>s->total-s->produced) {
      read_error="Zstandard large-stream exceeds declared entry size";
      large_discard(s);
      return 0;
    }
    if(z.pos) {
      s->last_output_at=s->produced;
      s->last_output_size=z.pos;
      s->produced+=z.pos;
      __atomic_fetch_add(&read_progress.decoded_bytes,(uint64_t)z.pos,__ATOMIC_RELAXED);
    }
    if(step==0) {
      if(s->produced!=s->total || wanted>s->produced) {
        read_error="Zstandard large-stream frame ended early";
        large_discard(s);
        return 0;
      }
      s->finished=1;
    }
    if(in.pos==prior && !z.pos) {
      if(++stalls>=2) {
        read_error="Zstandard large-stream made no progress";
        large_discard(s);
        return 0;
      }
    }else stalls=0;
    if(!s->finished && s->consumed==s->packed &&
       s->input_pos==s->input_size && !z.pos) {
      read_error="Zstandard large-stream missing frame terminator";
      large_discard(s);
      return 0;
    }
  }
  read_error="OK";
  return 1;
}
static int read_compressed(const unsigned char *e,uint32_t file,
                           uint64_t offset,void *dest,size_t length) {
  uint64_t total=rd64(e+8),at=rd64(e+16),packed=rd64(e+24);
  if(total<=16*1024*1024 && packed<=32*1024*1024) {
    read_error="Zstandard small-entry allocation/read failure";
    unsigned slot=2;
    for(unsigned k=0;k<2;k++)
      if(mounted.cached_file[k]==file && mounted.cached_bytes[k]){
        slot=k;break;
      }
    if(slot==2) {
      /* Empty slot first, then oldest. This code runs on the single
       * Nx2Host service thread, not the concurrent guest worker threads. */
      slot=mounted.cached_file[0]==UINT32_MAX?0:
           mounted.cached_file[1]==UINT32_MAX?1:
           mounted.cached_stamp[0]<=mounted.cached_stamp[1]?0:1;
      free(mounted.cached_bytes[slot]);
      mounted.cached_bytes[slot]=NULL;
      mounted.cached_file[slot]=UINT32_MAX;
      mounted.cached_size[slot]=0;
      unsigned char *raw=malloc((size_t)packed),*out=malloc((size_t)total);
      if(!raw||!out||!read_archive(at,raw,(size_t)packed)) {
        free(raw);free(out);return 0;
      }
      progress_phase(3);
      size_t n=ZSTD_decompress(out,(size_t)total,raw,(size_t)packed);
      if(ZSTD_isError(n)||n!=total){
        unsigned char header[8]={0};
        size_t available=(size_t)packed<sizeof(header)?(size_t)packed:sizeof(header);
        memcpy(header,raw,available);
        if(ZSTD_isError(n))
          snprintf(probe_details,sizeof(probe_details),
            "ZSTD code=%llu frame=%02x%02x%02x%02x%02x%02x%02x%02x",
            (unsigned long long)(0-(uint64_t)n),header[0],header[1],
            header[2],header[3],header[4],header[5],header[6],header[7]);
        else
          snprintf(probe_details,sizeof(probe_details),
            "ZSTD output=%llu expected=%llu frame=%02x%02x%02x%02x",
            (unsigned long long)n,(unsigned long long)total,
            header[0],header[1],header[2],header[3]);
        read_error=probe_details;
        free(raw);free(out);return 0;
      }
      __atomic_fetch_add(&read_progress.decoded_bytes,(uint64_t)n,__ATOMIC_RELAXED);
      free(raw);
      progress_phase(4);
      unsigned char hash[32];
      sha256(out,(size_t)total,hash);
      if(memcmp(hash,e+32,32)){
        read_error="Zstandard entry SHA-256 mismatch";free(out);return 0;
      }
      mounted.cached_bytes[slot]=out;
      mounted.cached_size[slot]=(size_t)total;
      mounted.cached_file[slot]=file;
    }
    mounted.cached_stamp[slot]=++mounted.cache_clock;
    progress_phase(4);
    memcpy(dest,mounted.cached_bytes[slot]+offset,length);
    __atomic_fetch_add(&read_progress.delivered_bytes,(uint64_t)length,__ATOMIC_RELAXED);
    return 1;
  }
  return read_large_compressed(e,file,offset,dest,length);
}
int nx2_nup_read(const char *path,uint64_t offset,void *dest,size_t length){
  uint32_t i=lookup_file(path);
  if(i==UINT32_MAX || (!dest&&length)){
    read_error="NUP path not present or destination invalid";return 0;
  }
  const unsigned char *e=mounted.files+(size_t)i*80;
  uint64_t size=rd64(e+8),at=rd64(e+16);
  if(offset>size||length>size-offset){
    read_error="NUP read exceeds entry bounds";return 0;
  }
  if(!length){read_error="OK";return 1;}
  progress_begin(i,e[64],length);
  if(e[64]==0) {
    /* Validate the absolute archive offset separately from virtual
     * extent bounds; an overflow must not alias another NUP entry. */
    read_error="Raw NUP archive offset overflow";
    int ok=at<=UINT64_MAX-offset &&
           read_archive(at+offset,dest,length);
    if(ok)__atomic_fetch_add(&read_progress.delivered_bytes,(uint64_t)length,__ATOMIC_RELAXED);
    if(ok)read_error="OK";
    progress_done();
    return ok;
  }
  int ok=read_compressed(e,i,offset,dest,length);
  if(ok)read_error="OK";
  progress_done();
  return ok;
}
void *nx2_nup_read_all(const char *path,size_t limit,size_t *length){
  uint64_t size=0;
  if(length)*length=0;
  if(!nx2_nup_file_info(path,&size,NULL)||!size||size>limit){
    read_error="NUP entry absent, empty, or exceeds size limit";return NULL;
  }
  void *p=malloc((size_t)size);
  if(!p){read_error="NUP output allocation failed";return NULL;}
  if(!nx2_nup_read(path,0,p,(size_t)size)){free(p);return NULL;}
  uint32_t i=lookup_file(path);
  unsigned char hash[32];
  sha256(p,(size_t)size,hash);
  if(i==UINT32_MAX ||
     memcmp(hash,mounted.files+(size_t)i*80+32,32)){
    read_error="NUP full-file SHA-256 mismatch";
    free(p);return NULL;
  }
  read_error="OK";
  if(length)*length=(size_t)size;
  return p;
}
static int walk_dir(uint32_t d,char path[1024],unsigned depth,
                    Nx2NupVisit visit,void *ctx,uint32_t stack[65]){
  if(depth>=64)return 0;
  for(unsigned i=0;i<depth;i++)if(stack[i]==d)return 0;
  stack[depth]=d;
  const unsigned char *node=mounted.dirs+(size_t)d*24;
  uint32_t start=rd32(node+8),count=rd32(node+12);
  size_t prefix=strlen(path);
  for(uint32_t i=0;i<count;i++){
    uint32_t child=rd32(mounted.children+(size_t)(start+i)*4),len=0;
    const unsigned char *name=name_for(&mounted,child,&len);
    if(!safe_name(name,len))return 0;
    if(len==8&&!memcmp(name,".listing",8))continue;
    size_t add=(prefix?1:0)+len;
    if(prefix+add>=1024)return 0;
    char *dst=path+prefix;
    if(prefix)*dst++='/';
    memcpy(dst,name,len);dst[len]=0;
    int directory=child<mounted.ndirs;
    uint64_t size=directory?0:rd64(mounted.files+(size_t)(child-mounted.ndirs)*80+8);
    if(!visit(ctx,path,directory,size))return 0;
    if(directory && !walk_dir(child,path,depth+1,visit,ctx,stack))return 0;
    path[prefix]=0;
  }
  return 1;
}
int nx2_nup_romfs_walk(Nx2NupVisit visit,void *context) {
  if(!mounted.f||mounted.romfs_dir>=mounted.ndirs||!visit)return 0;
  char path[1024]={0};uint32_t stack[65]={0};
  return walk_dir(mounted.romfs_dir,path,0,visit,context,stack);
}
