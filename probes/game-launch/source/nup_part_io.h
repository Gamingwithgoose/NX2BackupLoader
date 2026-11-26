/* Portable logical file reader for split NUP archives. */
#pragma once
#include "nupparts.h"
#include <stdlib.h>
#include <limits.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
#ifdef _WIN32
#define NX2_PART_SEEK _fseeki64
#else
#define NX2_PART_SEEK fseeko
#endif
typedef struct {
 NupPart parts[NUP_PARTS_MAX]; int count; uint64_t size,pos;
 FILE *files[NUP_PARTS_MAX];
#ifdef __SWITCH__
 FsFile native[NUP_PARTS_MAX]; int native_count;
#endif
} Nx2PartFile;
static inline void nx2_part_close(Nx2PartFile *f){
 if(!f)return;
#ifdef __SWITCH__
 for(int i=0;i<f->native_count;i++)fsFileClose(&f->native[i]);
#endif
 for(int i=0;i<f->count;i++)if(f->files[i])fclose(f->files[i]);
 free(f);
}
static inline Nx2PartFile *nx2_part_open(const char *path){
 Nx2PartFile *f=calloc(1,sizeof(*f));if(!f)return NULL;
 f->count=nup_parts_find(path,f->parts,NUP_PARTS_MAX);
 if(f->count<1)goto fail;
 for(int i=0;i<f->count;i++){
  if(!f->parts[i].size||f->parts[i].size>INT64_MAX-f->size)goto fail;
  f->size+=f->parts[i].size;
  f->files[i]=fopen(f->parts[i].path,"rb");if(!f->files[i])goto fail;
 }
 return f;
fail:nx2_part_close(f);return NULL;
}
static inline int nx2_part_seek(Nx2PartFile *f,int64_t off,int whence){
 if(!f)return -1;
 uint64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?f->pos:whence==SEEK_END?f->size:UINT64_MAX;
 if(base==UINT64_MAX)return -1;
 uint64_t delta=off<0?(uint64_t)(-(off+1))+1:(uint64_t)off;
 if((off<0&&delta>base)||(off>=0&&delta>f->size-base))return -1;
 f->pos=off<0?base-delta:base+delta;return 0;
}
static inline int64_t nx2_part_tell(Nx2PartFile *f){return f?(int64_t)f->pos:-1;}
static inline size_t nx2_part_read(void *dst,size_t unit,size_t count,Nx2PartFile *f){
 if(!f||!unit||count>SIZE_MAX/unit)return 0;
 size_t total=unit*count,done=0;unsigned char *p=dst;
 while(done<total){
  uint64_t at,left;int i=nup_parts_at(f->parts,f->count,f->pos,&at,&left);
  if(i<0)break;
  size_t take=total-done;if(left<take)take=(size_t)left;
  if(NX2_PART_SEEK(f->files[i],(int64_t)at,SEEK_SET))break;
  size_t got=fread(p+done,1,take,f->files[i]);f->pos+=got;done+=got;
  if(got!=take)break;
 }
 return done/unit;
}
#ifdef __SWITCH__
static inline int nx2_part_native_open(Nx2PartFile *f){
 FsFileSystem *sd=fsdevGetDeviceFileSystem("sdmc");if(!sd)return 0;
 for(int i=0;i<f->count;i++){
  if(strncmp(f->parts[i].path,"sdmc:/",6)||R_FAILED(fsFsOpenFile(sd,f->parts[i].path+5,FsOpenMode_Read,&f->native[i])))return 0;
  f->native_count++;
  s64 actual=0;if(R_FAILED(fsFileGetSize(&f->native[i],&actual))||actual<0||(uint64_t)actual!=f->parts[i].size)return 0;
 }
 return 1;
}
static inline uint32_t nx2_part_native_read(Nx2PartFile *f,uint64_t offset,void *dst,size_t len,uint64_t *got){
 *got=0;unsigned char *p=dst;
 if(!f||offset>f->size||len>f->size-offset)return 1;
 while(*got<len){
  uint64_t at,left,n=0;int i=nup_parts_at(f->parts,f->count,offset+*got,&at,&left);
  if(i<0||i>=f->native_count)return 1;
  uint64_t take=len-*got;if(left<take)take=left;
  Result rc=fsFileRead(&f->native[i],at,p+*got,take,FsReadOption_None,&n);
  *got+=n;if(R_FAILED(rc)||n!=take)return rc?rc:1;
 }return 0;
}
#endif
