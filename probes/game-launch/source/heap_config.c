/* SPDX-License-Identifier: GPL-2.0-only */
/* HeapDef discovery is informed by nx2-flow (MIT), runtime/game.c,
 * f0861ffbeda36d73df6d59dcbac4b95281eea526. This implementation uses our
 * folder/NUP union view and checked BYML/SARC bounds. No title-ID rules. */
#include "heap_config.h"
#include "heap_plan.h"
#include "nup.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define CONFIG_LIMIT (64U<<20)
typedef struct ZSTD_DCtx_s ZSTD_DCtx;
extern unsigned long long ZSTD_getFrameContentSize(const void *,size_t);
extern ZSTD_DCtx *ZSTD_createDCtx(void);
extern size_t ZSTD_freeDCtx(ZSTD_DCtx *);
extern size_t ZSTD_decompress_usingDict(ZSTD_DCtx *,void *,size_t,
                                      const void *,size_t,const void *,size_t);
extern unsigned ZSTD_isError(size_t);
static int span(uint64_t at,uint64_t n,uint64_t size){return at<=size&&n<=size-at;}
static uint32_t heap_u32(const unsigned char *p,int be){
 return be?((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]:
   (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint64_t heap_u64(const unsigned char *p,int be){
 uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(8*(be?7-i:i));return v;
}
static unsigned char *read_file(Nx2Host *host,const Nx2RomView *view,
                                uint32_t file,size_t *bytes){
 *bytes=0;const char *relative=nx2_rom_path(view,file);
 if(!relative)return NULL;
 uint64_t length=heap_u64(view->entries+32ULL*file+8,0);
 if(!length||length>CONFIG_LIMIT)return NULL;
 char path[2500];
 if(nx2_nup_active()){
  int n=snprintf(path,sizeof(path),"romfs/%s",relative);
  if(n<0||n>=(int)sizeof(path))return NULL;
  return nx2_nup_read_all(path,CONFIG_LIMIT,bytes);
 }
 uint32_t source=nx2_rom_source(view,file);
 if(source>1||(source&&!host->rom_base_root[0]))return NULL;
 int n=snprintf(path,sizeof(path),"sdmc:%s/%s",
                 source?host->rom_base_root:host->rom_root,relative);
 if(n<0||n>=(int)sizeof(path))return NULL;
 FILE *f=fopen(path,"rb");if(!f)return NULL;
 unsigned char *raw=malloc((size_t)length);
 size_t got=raw?fread(raw,1,(size_t)length,f):0;int closed=fclose(f);
 if(!raw||got!=length||closed){free(raw);return NULL;}
 *bytes=got;return raw;
}
static unsigned char *unpack(const void *raw,size_t bytes,const void *dict,
                             size_t dict_bytes,size_t *out_size){
 *out_size=0;
 unsigned long long n=ZSTD_getFrameContentSize(raw,bytes);
 if(n==ULLONG_MAX-1||!n|| (n!=ULLONG_MAX&&n>CONFIG_LIMIT))return NULL;
 size_t cap=n==ULLONG_MAX?CONFIG_LIMIT:(size_t)n;
 unsigned char *out=malloc(cap);if(!out)return NULL;
 ZSTD_DCtx *ctx=ZSTD_createDCtx();
 if(!ctx){free(out);return NULL;}
 size_t got=ZSTD_decompress_usingDict(ctx,out,cap,raw,bytes,dict,dict_bytes);
 ZSTD_freeDCtx(ctx);
 if(ZSTD_isError(got)){free(out);return NULL;}
 *out_size=got;return out;
}
void nx2_heap_configure(FILE *log,Nx2Launch *l,Nx2Host *host){
 const Nx2RomView *v=&l->rom;uint32_t chosen=UINT32_MAX,dictionary=UINT32_MAX;
 int best=-1;
 for(uint32_t i=0;i<v->count;i++){
  const char *p=nx2_rom_path(v,i);
  if(!strcmp(p,"Pack/ZsDic.pack.zs"))dictionary=i;
  if(strncmp(p,"System/HeapSize/",16)||!strstr(p,".HeapDef.byml"))continue;
  int score=(strstr(p,"Ounce")?2:0)+(strstr(p,"Product")?1:0);
  if(score>best||(score==best&&strcmp(p,nx2_rom_path(v,chosen))<0))best=score,chosen=i;
 }
 if(chosen==UINT32_MAX){fprintf(log,"HEAP_CONFIG version=1 status=absent policy=bounded_physical\n");fflush(log);return;}
 size_t raw_size=0,plain_size=0;
 unsigned char *raw=read_file(host,v,chosen,&raw_size),*plain=NULL;
 if(raw&&raw_size>=4&&heap_u32(raw,0)==0xfd2fb528U){
  plain=unpack(raw,raw_size,NULL,0,&plain_size);
  if(!plain&&dictionary!=UINT32_MAX){
   size_t pack_size=0;unsigned char *pack=read_file(host,v,dictionary,&pack_size);
   if(pack&&pack_size>=4&&heap_u32(pack,0)==0xfd2fb528U){
    size_t n=0;unsigned char *decoded=unpack(pack,pack_size,NULL,0,&n);
    free(pack);pack=decoded;pack_size=n;
   }
   if(pack&&pack_size>=0x20&&!memcmp(pack,"SARC",4)){
    int be=pack[6]==0xfe&&pack[7]==0xff;
    uint32_t header=be?((uint32_t)pack[4]<<8)|pack[5]:pack[4]|((uint32_t)pack[5]<<8);
    uint32_t data=heap_u32(pack+12,be);
    if(span(header,12,pack_size)&&!memcmp(pack+header,"SFAT",4)){
     uint32_t count=be?((uint32_t)pack[header+6]<<8)|pack[header+7]:pack[header+6]|((uint32_t)pack[header+7]<<8);
     if(count<=256&&span((uint64_t)header+12,16ULL*count,pack_size))
      for(uint32_t k=0;k<count&&!plain;k++){
       const unsigned char *e=pack+header+12+16ULL*k;
       uint32_t start=heap_u32(e+8,be),end=heap_u32(e+12,be);
       if(start<=end&&span((uint64_t)data+start,end-start,pack_size))
        plain=unpack(raw,raw_size,pack+data+start,end-start,&plain_size);
      }
    }
   }
   free(pack);
  }
  free(raw);
 }else{plain=raw;plain_size=raw_size;}
 uint64_t apart=0;int parsed=nx2_heapdef_reserve(plain,plain_size,&apart);free(plain);
 uint64_t total=0,used=0;
 Result a=svcGetInfo(&total,21,CUR_PROCESS_HANDLE,0);
 Result b=svcGetInfo(&used,22,CUR_PROCESS_HANDLE,0);
 if(parsed==1&&apart&&R_SUCCEEDED(a)&&R_SUCCEEDED(b)&&used<=total&&
    nx2_process_heap.game_bytes<=UINT64_MAX-apart){
  /* The logical layout includes reserved virtual space. It is not a grant
   * of physical pages: every heap/map allocation still reaches the kernel. */
  l->guest_virtual_reserve=apart;
  l->guest_logical_budget=nx2_process_heap.game_bytes+apart;
  if(l->guest_logical_budget>total)l->guest_logical_budget=total;
  l->guest_program_bytes=used>nx2_process_heap.host_bytes?used-nx2_process_heap.host_bytes:0;
  l->physical_floor=UINT64_C(64)<<20;
 }
 fprintf(log,"HEAP_CONFIG version=1 path=%s parsed=%d virtual_reserve=%llu logical_budget=%llu program_bytes=%llu physical_floor=%llu total_rc=%08x used_rc=%08x physical_success_never_faked=1\n",
   nx2_rom_path(v,chosen),parsed,(unsigned long long)l->guest_virtual_reserve,
   (unsigned long long)l->guest_logical_budget,(unsigned long long)l->guest_program_bytes,
   (unsigned long long)(l->physical_floor?l->physical_floor:NX2_NATIVE_PHYSICAL_FLOOR_BYTES),a,b);
 fflush(log);
}
