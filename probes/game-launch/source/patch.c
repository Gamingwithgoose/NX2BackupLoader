/* SPDX-License-Identifier: GPL-2.0-only */
#include "patch.h"
static uint32_t word(const unsigned char *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void put(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static int branch(int64_t delta,uint32_t *out){if((delta&3)||delta<-(1LL<<27)||delta>=(1LL<<27))return 0;*out=0x14000000|((uint64_t)(delta>>2)&0x3ffffff);return 1;}
int nx2_patch_gate(unsigned char *text,uint32_t original_size,uint32_t padded,uint32_t *cursor,uint32_t at,uintptr_t target,uintptr_t guest,uintptr_t *original){
 uint32_t needed=original?32:16;
 if(!text||!cursor||at>original_size||original_size-at<4||(*cursor&15)||*cursor<original_size||*cursor>padded||padded-*cursor<needed)return 0;
 uint32_t off=*cursor,b;if(!branch((int64_t)off-at,&b))return 0;uint32_t ins=word(text+at);
 /* A copied prologue must not contain a PC-relative instruction. Known hooks
  * use PACIBSP or a regular SVC wrapper. Branch entries are relocated below. */
 if(original){
  uint32_t relocated=ins;
  if((ins&0x7c000000)==0x14000000){int64_t imm=(int64_t)(ins&0x3ffffff);if(imm&(1<<25))imm-=1<<26;int64_t d=(int64_t)at+(imm<<2)-(off+16);if(!branch(d,&relocated))return 0;if(ins&0x80000000)return 0;}
  else if((ins&0x1f000000)==0x10000000||(ins&0x3b000000)==0x18000000||(ins&0x7e000000)==0x34000000||(ins&0x7e000000)==0x36000000||(ins&0xff000010)==0x54000000)return 0;
  put(text+off+16,relocated);uint32_t back;if(!branch((int64_t)at+4-(off+20),&back))return 0;put(text+off+20,back);*original=guest+off+16;
 }
 put(text+off,0x58000050);put(text+off+4,0xd61f0200);for(unsigned i=0;i<8;i++)text[off+8+i]=(unsigned char)(target>>(8*i));put(text+at,b);*cursor+=needed;return 1;
}
