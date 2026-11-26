/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
static inline uint32_t nx2_meta_u32(const unsigned char *p){
 return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
/* Do not mistake a real game's name beginning with META for an NPDM. */
static inline int nx2_meta_npdm(const unsigned char *p,size_t n){
 if(!p||n<128||memcmp(p,"META",4))return 0;
 uint32_t at=nx2_meta_u32(p+0x70),bytes=nx2_meta_u32(p+0x74);
 return at>=128&&at<=n&&bytes>=32&&bytes<=n-at&&!memcmp(p+at,"ACI0",4);
}
/* Published producers have used both zero- and one-based fact tags. Anchor
 * the namespace to a structurally valid NPDM, never to a display string.
 * Observation: nx2-flow/menu/source/nup.cpp at 9ad10e2 (GPL-2.0-or-later).
 * This checked implementation is original; standard zero-based is default. */
static inline unsigned nx2_meta_base(const unsigned char *p,size_t n){
 size_t at=0;
 while(n-at>=8){
  unsigned type=p[at]|((unsigned)p[at+1]<<8),flags=p[at+2]|((unsigned)p[at+3]<<8);
  uint32_t bytes=nx2_meta_u32(p+at+4);at+=8;
  if(flags||bytes>n-at)break;
  if(type<=1&&nx2_meta_npdm(p+at,bytes))return type;
  at+=bytes;
 }
 return 0;
}
