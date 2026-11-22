/* SPDX-License-Identifier: GPL-2.0-only */
/* cc -std=c11 -O2 -Wall -Wextra -Werror -I../source
 *    test_patch_branch_relocation.c ../source/patch.c -o test_patch_branch_relocation
 * Portable fixtures: no AArch64 execution or game-specific patches. */
#include "patch.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static int fail=0;
static void check(int cond,const char*name){
 if(!cond){fprintf(stderr,"FAILED: %s\n",name);fail++;}
}
static uint32_t rd(const unsigned char*p){
 return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void wr(unsigned char*p,uint32_t x){
 for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(x>>(i*8));
}
static uint32_t rel(uint32_t source,uint32_t target,int link){
 int64_t delta=(int64_t)target-source;
 if((delta&3)||delta<-(1LL<<27)||delta>=(1LL<<27)){
  fprintf(stderr,"Invalid test fixture branch\n");exit(1);
 }
 return (link?0x94000000U:0x14000000U)|
        ((uint32_t)(delta>>2)&0x3ffffff);
}
static uint32_t dest(uint32_t source,uint32_t ins){
 int64_t imm=ins&0x3ffffff;
 if(imm&(1LL<<25))imm-=1LL<<26;
 return (uint32_t)((int64_t)source+imm*4);
}
static uint32_t veneer_dest(uint32_t at,uint32_t adrp,uint32_t add){
 int64_t pages=((int64_t)((adrp>>5)&0x7ffff)<<2)|((adrp>>29)&3);
 if(pages&(1LL<<20))pages-=1LL<<21;
 return (uint32_t)(((int64_t)(at&~4095U)+(pages*4096))+
                    ((add>>10)&4095U));
}
int main(void){
 /* Starfox-sized module forces the absolute-entry path: its nnMain
  * begins at 0x817e20, while available text padding is near 0xb22b280. */
 const uint32_t at=0x817e20,base=0xb22b280,padded=0xb22c000;
 unsigned char*text=calloc(1,(size_t)padded);
 if(!text){fprintf(stderr,"fixture allocation failed\n");return 1;}
 uintptr_t original=0;
 const uintptr_t gate=UINT64_C(0x1234567812345678);
 uint32_t cursor=base,target=at+100U*1024U*1024U;
 wr(text+at,0x90055600);wr(text+at+4,0xd503201f);
 wr(text+at+8,rel(at+8,target,1));wr(text+at+12,0xd503201f);
 check(nx2_patch_gate(text,base,padded,&cursor,at,gate,0,&original),
       "BL in 16-byte prologue relocates");
 check(!strcmp(nx2_patch_method(),"absolute_entry_branch"),"direct BL method");
 check(original==base&&cursor==base+32,"original entry and exact cursor");
 check((rd(text+base+8)&0xfc000000)==0x94000000 &&
       dest(base+8,rd(text+base+8))==target,"BL preserves link and destination");
 check(rd(text+at)==0x58000050&&rd(text+at+4)==0xd61f0200,
       "original function uses guarded absolute gate");
 /* A local B target inside the replaced block must jump to copied code,
  * not to the overwritten literal/jump stub. */
 cursor=base;memset(text+base,0,64);
 wr(text+at,0x90055600);wr(text+at+4,rel(at+4,at+12,0));
 wr(text+at+8,0xd503201f);wr(text+at+12,0xd503201f);
 check(nx2_patch_gate(text,base,padded,&cursor,at,gate,0,&original),
       "local unconditional branch is safe");
 check(dest(base+4,rd(text+base+4))==base+12,
       "local branch redirects to relocated instruction");
 /* Original BL can reach a function far below the trampoline; a 16-byte
  * ADRP/ADD/BR island must preserve the return address to copied code. */
 cursor=base;memset(text+base,0,80);
 target=at+0x2000;
 wr(text+at,0x90055600);wr(text+at+4,0xd503201f);
 wr(text+at+8,rel(at+8,target,1));wr(text+at+12,0xd503201f);
 check(nx2_patch_gate(text,base,padded,&cursor,at,gate,0,&original),
       "out-of-range BL uses veneer");
 check(!strcmp(nx2_patch_method(),"absolute_entry_branch_veneer")&&
       cursor==base+48,"veneer counted in padding");
 check((rd(text+base+8)&0xfc000000)==0x94000000 &&
       dest(base+8,rd(text+base+8))==base+32,"BL jumps to local island");
 check(rd(text+base+40)==0xd61f0220 &&
       veneer_dest(base+32,rd(text+base+32),rd(text+base+36))==target,
       "veneer reconstructs original destination");
 /* Preserve conservative handling of unsafe PC-relative literal loads. */
 cursor=base;memset(text+base,0,80);
 wr(text+at,0x90055600);wr(text+at+4,0x58000020);
 wr(text+at+8,0xd503201f);wr(text+at+12,0xd503201f);
 check(!nx2_patch_gate(text,base,padded,&cursor,at,gate,0,&original),
       "unsupported literal load rejected");
 check(cursor==base&&rd(text+at)==0x90055600,
       "failed relocation does not modify original entry");
 /* Large guest modules can contain many inline nonreturning Break SVCs.
  * Multiple call sites branch to the same bridge without modifying their
  * adjacent instructions or allocating additional trampolines. */
 unsigned char fixture[512]={0};
 wr(fixture+0x40,0xd40004c1U);
 wr(fixture+0x80,0xd40004c1U);
 wr(fixture+0x84,0xd503201fU);
 check(nx2_patch_shared_break(fixture,0x100,0x200,0x40,0x100),
       "first inline Break shares trampoline");
 check(nx2_patch_shared_break(fixture,0x100,0x200,0x80,0x100),
       "second inline Break shares trampoline");
 check(dest(0x40,rd(fixture+0x40))==0x100 &&
       dest(0x80,rd(fixture+0x80))==0x100 &&
       rd(fixture+0x84)==0xd503201fU,
       "shared Break branches retain adjacent instructions");
 check(!nx2_patch_shared_break(fixture,0x100,0x200,0x84,0x100),
       "reject non-Break instruction");
 wr(fixture+0x80,0xd40004c1U);
 check(!nx2_patch_shared_break(fixture,0x100,0x200,0x80,0x80),
       "reject trampoline inside original text");
 check(!nx2_patch_shared_break(fixture,0x100,0x200,0x80,0x201),
       "reject unaligned trampoline");
 free(text);
 puts(fail?"Patch branch relocation fixtures: FAIL":
           "Patch branch relocation fixtures: PASS");
 return !!fail;
}
