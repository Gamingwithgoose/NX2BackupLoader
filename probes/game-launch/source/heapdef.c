/* SPDX-License-Identifier: GPL-2.0-only */
#include "heapdef.h"
#include <string.h>
static int span(uint64_t at,uint64_t n,uint64_t size){return at<=size&&n<=size-at;}
static uint32_t heap_u32(const unsigned char *p,int be){
 return be?((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]:
   (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint64_t heap_u64(const unsigned char *p,int be){
 uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(8*(be?7-i:i));return v;
}
/* BYML container header: type byte plus an endian-dependent 24-bit count. */
static uint32_t count24(const unsigned char *p,int be){
 return be?((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]:
   p[1]|((uint32_t)p[2]<<8)|((uint32_t)p[3]<<16);
}
static int lookup(const unsigned char *b,size_t size,int be,uint32_t keys,
                  uint32_t map,const char *key,uint32_t *value,unsigned *type){
 if(!span(keys,4,size)||!span(map,4,size)||b[keys]!=0xc2||b[map]!=0xc1)return -1;
 uint32_t nk=count24(b+keys,be),nm=count24(b+map,be);
 if(!span((uint64_t)keys+4,4ULL*(nk+1ULL),size)||
    !span((uint64_t)map+4,8ULL*nm,size))return -1;
 for(uint32_t i=0;i<nm;i++){
  const unsigned char *e=b+map+4+8ULL*i;
  uint32_t index=be?((uint32_t)e[0]<<16)|((uint32_t)e[1]<<8)|e[2]:
    e[0]|((uint32_t)e[1]<<8)|((uint32_t)e[2]<<16);
  if(index>=nk)return -1;
  uint32_t at=heap_u32(b+keys+4+4ULL*index,be);
  uint32_t end=heap_u32(b+keys+8+4ULL*index,be);
  if(at>=end||!span((uint64_t)keys+at,end-at,size))return -1;
  const unsigned char *s=b+keys+at;
  const unsigned char *nul=memchr(s,0,end-at);
  if(!nul)return -1;
  if((size_t)(nul-s)==strlen(key)&&!memcmp(s,key,(size_t)(nul-s))){
   *type=e[3];*value=heap_u32(e+4,be);return 1;
  }
 }
 return 0;
}
int nx2_heapdef_reserve(const void *data,size_t size,uint64_t *reserve){
 if(!reserve)return -1;
 *reserve=0;
 const unsigned char *b=data;
 if(!b||size<16||!((b[0]=='Y'&&b[1]=='B')||(b[0]=='B'&&b[1]=='Y')))return -1;
 int be=b[0]=='B';uint32_t keys=heap_u32(b+4,be),map=heap_u32(b+12,be),value=0;
 unsigned type=0;const char *names[]={"Heaps","VirtualAddressMemory","Size"};
 for(unsigned i=0;i<3;i++){
  int rc=lookup(b,size,be,keys,map,names[i],&value,&type);
  if(rc!=1)return rc;
  if(i<2){if(type!=0xc1)return -1;map=value;}
 }
 uint64_t kb;
 if(type==0xd1||type==0xd3){
  if(type==0xd1&&(value&0x80000000U))return -1;
  kb=value;
 }else if(type==0xd4||type==0xd5){
  if(!span(value,8,size))return -1;
  kb=heap_u64(b+value,be);
  if(type==0xd4&&(kb>>63))return -1;
 }else return -1;
 if(kb>(UINT64_C(16)<<20))return -1;
 *reserve=kb<<10;return 1;
}
