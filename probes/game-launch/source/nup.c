/* SPDX-License-Identifier: GPL-2.0-only */
#include "nup.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
int nx2_nup_probe(const char *path, uint64_t *title_id, uint64_t *files) {
  unsigned char header[128], digest[32];
  FILE *f=fopen(path,"rb");
  if (!f) return 0;
  int ok=0;
  unsigned char *toc=NULL;
  if (fread(header,1,sizeof(header),f)!=sizeof(header)) goto done;
  if (memcmp(header,"NUP\1",4) || header[4]!=0x80 || header[5] ||
      header[6]!=1 || header[7] || rd32(header+0x4c) ||
      rd64(header+8)!=128) goto done;
  uint64_t toc_size=rd64(header+0x10), blob=rd64(header+0x38),
           file_size=rd64(header+0x40);
  uint32_t packages=rd32(header+0x48);
  if (!packages || packages>4096 || toc_size<48 || toc_size>128*1024*1024 ||
      blob%16 || blob<128 || toc_size>blob-128 || file_size<blob ||
      fseek(f,0,SEEK_END) || ftell(f)<0 || (uint64_t)ftell(f)!=file_size ||
      fseek(f,128,SEEK_SET)) goto done;
  toc=malloc((size_t)toc_size);
  if (!toc || fread(toc,1,(size_t)toc_size,f)!=(size_t)toc_size) goto done;
  sha256(toc,(size_t)toc_size,digest);
  if (memcmp(digest,header+0x18,32) || memcmp(toc,"TOC\1",4) ||
      rd32(toc+4)!=packages || rd64(toc+0x28) ||
      toc_size<48+(uint64_t)packages*64) goto done;
  uint64_t dirs=rd64(toc+8), nfiles=rd64(toc+16);
  uint64_t pool_off=rd64(toc+0x18), pool_len=rd64(toc+0x20);
  uint64_t start=48+(uint64_t)packages*64;
  if (!dirs || dirs>262144 || nfiles>262144 ||
      dirs>(toc_size-start)/24) goto done;
  start+=dirs*24;
  if (nfiles>(toc_size-start)/80) goto done;
  start+=nfiles*80;
  if (pool_off<start || pool_off>toc_size || pool_len>toc_size-pool_off) goto done;
  for (uint32_t i=0;i<packages;i++) {
    const unsigned char *p=toc+48+(size_t)i*64;
    uint32_t root=rd32(p+8), exefs=rd32(p+12), romfs=rd32(p+16);
    if (p[24]>=5 || p[25]&~1 || p[26] || p[27] || rd32(p+28) ||
        root>=dirs || (exefs!=UINT32_MAX && exefs>=dirs) ||
        (romfs!=UINT32_MAX && romfs>=dirs)) goto done;
    if (p[24]==0 && exefs!=UINT32_MAX && romfs!=UINT32_MAX) {
      if (title_id) *title_id=rd64(p);
      if (files) *files=nfiles;
      ok=1;
      break;
    }
  }
done:
  free(toc);
  fclose(f);
  return ok;
}
