/* SPDX-License-Identifier: GPL-2.0-only */
#include "rom_view.h"
static uint64_t q(const void *p){const unsigned char *b=p;uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)b[i]<<(8*i);return v;}
static uint32_t w(const void *v){const unsigned char *p=v;uint32_t r=0;for(unsigned i=0;i<4;i++)r|=(uint32_t)p[i]<<(8*i);return r;}
static int span(uint64_t p,uint64_t n,uint64_t size){return p<=size&&n<=size-p;}
static int path_ok(const unsigned char *p,size_t n){
 if(!n||n>=1024||p[0]=='/'||p[n])return 0;
 size_t start=0;
 for(size_t i=0;i<=n;i++){
  if(i<n&&(!p[i]||p[i]=='\\'||p[i]==':'))return 0;
  if(i==n||p[i]=='/'){
   size_t len=i-start;if(!len||(len==1&&p[start]=='.')||(len==2&&p[start]=='.'&&p[start+1]=='.'))return 0;start=i+1;
  }
 }return 1;
}
int nx2_rom_view(const void *data,size_t size,Nx2RomView *out){
 const unsigned char *p=data;static const unsigned char magic[8]={'N','X','2','R','O','M','0','1'};
 if(!p||!out||size<80)return 0;
 for(unsigned i=0;i<8;i++)if(p[i]!=magic[i])return 0;
 if(w(p+8)!=1||w(p+12)!=80||w(p+36)!=32)return 0;
 uint64_t ms=q(p+16),total=q(p+24),eo=q(p+40),so=q(p+48),ss=q(p+56),mo=q(p+64),base=q(p+72);uint32_t n=w(p+32);
 if(!n||n>262144||!span(eo,(uint64_t)n*32,size)||!span(so,ss,size)||!span(mo,ms,size)||ms<80||ms>base||base>total||total>INT64_MAX)return 0;
 if(eo!=80||so<eo+(uint64_t)n*32||mo<so+ss)return 0;
 Nx2RomView v={p,p+eo,p+so,p+mo,ms,total,base,ss,n};
 uint64_t previous=base;
 for(uint32_t i=0;i<n;i++){
  const unsigned char *e=v.entries+32*i;uint64_t at=q(e),len=q(e+8);uint32_t po=w(e+16),pl=w(e+20);
  if(at<previous||!span(at,len,total)||po>=ss||pl>=ss-po||!path_ok(v.strings+po,pl)||q(e+24))return 0;
  previous=at+len;
 }
 if(q(v.metadata)!=80||q(v.metadata+72)!=base)return 0;
 for(unsigned i=1;i<9;i+=2)if(!span(q(v.metadata+8*i),q(v.metadata+8*(i+1)),ms))return 0;
 *out=v;return 1;
}
const char *nx2_rom_path(const Nx2RomView *v,uint32_t i){if(!v||i>=v->count)return 0;return (const char *)v->strings+w(v->entries+32*i+16);}
uint32_t nx2_rom_read(const Nx2RomView *v,uint64_t offset,void *buffer,uint64_t bytes,Nx2RomReadFile read,void *context){
 if(!v||!span(offset,bytes,v->total)||(!buffer&&bytes))return 0x2f5e02;
 unsigned char *out=buffer;
 /* A 40+ MiB IStorage read currently monopolizes the host in 4-MiB
  * chunks, starving other guest RPCs on slower SD storage. Split only
  * large requests into 1-MiB chunks, preserving exact read semantics
  * and the existing fast path for small, normal asset requests. */
 const uint64_t fair_chunk=bytes>0x400000U?0x100000U:0x400000U;
 while(bytes){
  uint64_t amount=bytes;
  if(amount>fair_chunk)amount=fair_chunk;
  if(offset<v->metadata_size){if(amount>v->metadata_size-offset)amount=v->metadata_size-offset;for(uint64_t i=0;i<amount;i++)out[i]=v->metadata[offset+i];}
  else{
   uint32_t lo=0,hi=v->count;
   while(lo<hi){uint32_t mid=lo+(hi-lo)/2;if(q(v->entries+32*mid)<=offset)lo=mid+1;else hi=mid;}
   const unsigned char *e=lo?v->entries+32*(lo-1):0;
   if(e&&offset-q(e)<q(e+8)){
    if(amount>q(e+8)-(offset-q(e)))amount=q(e+8)-(offset-q(e));
    if(!read)return 0x2f5e02;
    uint32_t rc=read(context,lo-1,offset-q(e),out,amount);if(rc)return rc;
   }else{
    uint64_t next=lo<v->count?q(v->entries+32*lo):v->total;if(amount>next-offset)amount=next-offset;
    if(!amount)return 0x2f5e02;
    for(uint64_t i=0;i<amount;i++)out[i]=0;
   }
  }
  offset+=amount;out+=amount;bytes-=amount;
 }return 0;
}
