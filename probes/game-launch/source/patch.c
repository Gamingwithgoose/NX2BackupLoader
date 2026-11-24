/* SPDX-License-Identifier: GPL-2.0-only */
#include "patch.h"
#include <stdint.h>

static const char *patch_reason = "not_attempted";
static const char *patch_method = "none";
const char *nx2_patch_failure_reason(void) { return patch_reason; }
const char *nx2_patch_method(void) { return patch_method; }

static uint32_t word(const unsigned char *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
         ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static void put(unsigned char *p,uint32_t v) {
  for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));
}
static void put64(unsigned char *p,uintptr_t v) {
  for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(v>>(i*8));
}
static int branch(int64_t delta,uint32_t *out) {
  if((delta&3)||delta<-(1LL<<27)||delta>=(1LL<<27))return 0;
  *out=0x14000000|((uint64_t)(delta>>2)&0x3ffffff);
  return 1;
}
/* AArch64 PC-relative instructions cannot be copied verbatim to a remote
 * trampoline. The fast hook relocates one B or ADRP instruction; the
 * four-instruction absolute-entry fallback conservatively requires a
 * position-independent prologue. It never silently relocates unknown PCs. */
static int pc_relative(uint32_t ins) {
  return (ins&0x7c000000)==0x14000000 ||
         (ins&0x1f000000)==0x10000000 ||
         (ins&0x3b000000)==0x18000000 ||
         (ins&0x7e000000)==0x34000000 ||
         (ins&0x7e000000)==0x36000000 ||
         (ins&0xff000010)==0x54000000;
}
static int relocate_one(uint32_t ins,uint32_t from,uint32_t to,
                        uint32_t *relocated) {
  *relocated=ins;
  if((ins&0x7c000000)==0x14000000){
    /* B only. BL would change LR to the trampoline return address. */
    if(ins&0x80000000)return 0;
    int64_t imm=ins&0x3ffffff;
    if(imm&(1LL<<25))imm-=1LL<<26;
    uint32_t b;
    if(!branch((int64_t)from+(imm*4)-to,&b))return 0;
    *relocated=b;return 1;
  }
  if((ins&0x9f000000)==0x90000000) {
    /* ADRP: rebase the original target page relative to the trampoline.
     * Both offsets share a 4 KiB-aligned NSO mapping base. */
    int64_t imm=((int64_t)((ins>>5)&0x7ffff)<<2)|((ins>>29)&3);
    if(imm&(1LL<<20))imm-=1LL<<21;
    int64_t pages=imm+((int64_t)(from>>12)-(int64_t)(to>>12));
    if(pages<-(1LL<<20)||pages>=(1LL<<20))return 0;
    uint32_t bits=(uint32_t)(pages&0x1fffff);
    *relocated=(ins&0x9f00001f)|((bits&3)<<29)|((bits>>2)<<5);
    return 1;
  }
  if((ins&0x9f000000)==0x10000000) {
    /* ADR: the relocated instruction must still fit its 21-bit range. */
    int64_t imm=((int64_t)((ins>>5)&0x7ffff)<<2)|((ins>>29)&3);
    if(imm&(1LL<<20))imm-=1LL<<21;
    int64_t displacement=(int64_t)from+imm-to;
    if(displacement<-(1LL<<20)||displacement>=(1LL<<20))return 0;
    uint32_t bits=(uint32_t)(displacement&0x1fffff);
    *relocated=(ins&0x9f00001f)|((bits&3)<<29)|((bits>>2)<<5);
    return 1;
  }
  return !pc_relative(ins);
}
/* An exported function can consist solely of an unconditional B to its
 * implementation, followed by alignment padding. When a remote trampoline
 * cannot be reached, preserve the original destination directly instead of
 * copying a branch or overwriting the next neighboring function. No title
 * names, fixed offsets or game-specific assumptions are involved. */
static int branch_alias_entry(unsigned char *text,uint32_t original_size,
                              uint32_t at,uintptr_t target,
                              uintptr_t *original) {
  if(original_size-at<16) {
    patch_reason="branch_alias_has_less_than_16_bytes";return 0;
  }
  uint32_t first=word(text+at);
  if((first&0xfc000000)!=0x14000000) {
    patch_reason="not_an_unconditional_B_alias";return 0;
  }
  int64_t displacement=(int64_t)(first&0x3ffffff);
  if(displacement&(1LL<<25))displacement-=1LL<<26;
  int64_t destination=(int64_t)at+(displacement*4);
  if(destination<0 || (uint64_t)destination>=original_size ||
     (destination>=(int64_t)at && destination<(int64_t)at+16)) {
    patch_reason="branch_alias_destination_not_safe";return 0;
  }
  for(unsigned i=1;i<4;i++){
    uint32_t ins=word(text+at+4*i);
    if(ins!=0 && ins!=0xd503201f) {
      patch_reason="branch_alias_padding_not_isolated";return 0;
    }
  }
  if(original)*original=(uintptr_t)destination;
  put(text+at,0x58000050);  /* ldr x16, [pc,#8] */
  put(text+at+4,0xd61f0200); /* br x16 */
  put64(text+at+8,target);
  patch_reason="OK";
  patch_method="absolute_branch_alias";
  return 1;
}
/* Rebase an unconditional AArch64 B/BL stolen from a 16-byte entry.
 * A BL must retain the link bit: the callee returns to the next copied
 * instruction in the trampoline, rather than into the overwritten gate.
 * If the original destination lies inside those 16 bytes, route it into
 * the copied instructions. For distant original targets, use a nearby
 * 16-byte ADRP/ADD/BR veneer. This is valid for the current NSO mapping
 * because its base is page-aligned and all these offsets share that base.
 * IP1 (x17) is the ABI-designated linker scratch register. Unsupported
 * PC-relative instructions still refuse the hook rather than corrupt it. */
static int relocate_entry_branch(uint32_t ins,uint32_t from,uint32_t to,
                                 uint32_t original_at,uint32_t copy_at,
                                 uint32_t veneer_at,uint32_t *out,
                                 uint32_t veneer[4],int *use_veneer) {
  int64_t imm=(int64_t)(ins&0x3ffffff);
  if(imm&(1LL<<25))imm-=1LL<<26;
  int64_t dest=(int64_t)from+imm*4;
  if(dest<0||dest>UINT32_MAX)return 0;
  int64_t mapped=dest;
  if(dest>=original_at && dest<(int64_t)original_at+16)
    mapped=(int64_t)copy_at+(dest-original_at);
  uint32_t b=0;
  if(branch(mapped-to,&b)){
    *out=b|(ins&0x80000000U);
    *use_veneer=0;
    return 1;
  }
  /* A branch from the copied prologue can always reach its own island.
   * Use a base-relative ADRP, avoiding an absolute address relocation. */
  int64_t pages=(mapped>>12)-((int64_t)veneer_at>>12);
  if(pages<-(1LL<<20)||pages>=(1LL<<20) ||
     !branch((int64_t)veneer_at-to,&b))return 0;
  uint32_t bits=(uint32_t)(pages&0x1fffff);
  veneer[0]=0x90000011|((bits&3)<<29)|((bits>>2)<<5); /* adrp x17 */
  veneer[1]=0x91000231|(((uint32_t)mapped&4095U)<<10); /* add x17 */
  veneer[2]=0xd61f0220; /* br x17 */
  veneer[3]=0xd503201f; /* nop */
  *out=b|(ins&0x80000000U);
  *use_veneer=1;
  return 1;
}
/* Common fallback for a hooked entry point beyond the immediate B range.
 * Replace the first 16 bytes with an absolute branch to the host gate.
 * Save four safe original instructions in the executable text padding,
 * then return using ADRP/ADD/BR (mapping-base independent, up to +/-4 GiB).
 * This deliberately declines PC-relative prologues rather than corrupting
 * them. Fast 4-byte hooks remain the default for existing working titles. */
static int absolute_entry(unsigned char *text,uint32_t original_size,
                          uint32_t padded,uint32_t *cursor,uint32_t at,
                          uintptr_t target,uintptr_t guest,
                          uintptr_t *original) {
  if(!original){patch_reason="absolute_fallback_requires_original";return 0;}
  if(original_size-at<16){patch_reason="prologue_shorter_than_16";return 0;}
  if(padded-*cursor<32){patch_reason="insufficient_executable_trampoline_padding";return 0;}
  uint32_t off=*cursor;
  uint32_t saved[4],islands[4][4]={{0}};
  unsigned island_count=0,branch_count=0;
  for(unsigned i=0;i<4;i++){
    uint32_t ins=word(text+at+4*i);
    /* B and BL preserve control flow, with an IP1 veneer only if needed.
     * An unsupported conditional branch, literal load or ADR variant
     * still fails without overwriting a single instruction. */
    if((ins&0x7c000000)==0x14000000){
      uint64_t need=32ULL+16ULL*(island_count+1);
      int used=0;
      if(!relocate_entry_branch(ins,at+4*i,off+4*i,at,off,
               off+32+16*island_count,&saved[i],islands[island_count],
               &used)){
        patch_reason="unsupported_entry_branch_relocation";
        return 0;
      }
      if(used){
        if((uint64_t)padded-off<need){
          patch_reason="insufficient_branch_veneer_padding";
          return 0;
        }
        island_count++;
      }
      branch_count++;
    }else if(!relocate_one(ins,at+4*i,off+4*i,&saved[i])){
      patch_reason="unsupported_pc_relative_16_byte_prologue";
      return 0;
    }
  }
  int64_t pages=(int64_t)((at+16)>>12)-(int64_t)((off+16)>>12);
  if(pages<-(1LL<<20)||pages>=(1LL<<20)){
    patch_reason="trampoline_return_adrp_out_of_range";return 0;
  }
  for(unsigned i=0;i<4;i++)put(text+off+i*4,saved[i]);
  for(unsigned i=0;i<island_count;i++)
    for(unsigned j=0;j<4;j++)
      put(text+off+32+16*i+4*j,islands[i][j]);
  uint32_t imm=(uint32_t)(pages&0x1fffff);
  put(text+off+16,0x90000010|((imm&3)<<29)|((imm>>2)<<5));
  put(text+off+20,0x91000210|(((at+16)&4095)<<10));
  put(text+off+24,0xd61f0200);  /* br x16 */
  put(text+off+28,0xd503201f);  /* nop */
  put(text+at,0x58000050);  /* ldr x16, [pc,#8] */
  put(text+at+4,0xd61f0200); /* br x16 */
  put64(text+at+8,target);
  *original=guest+off;
  *cursor+=32+16*island_count;
  patch_reason="OK";
  patch_method=island_count?"absolute_entry_branch_veneer":
                      branch_count?"absolute_entry_branch":"absolute_entry";
  return 1;
}
int nx2_patch_gate(unsigned char *text,uint32_t original_size,uint32_t padded,
                   uint32_t *cursor,uint32_t at,uintptr_t target,
                   uintptr_t guest,uintptr_t *original) {
  patch_reason="invalid_patch_site";
  patch_method="none";
  if(!text||!cursor||at>original_size||original_size-at<4)return 0;
  if(!original_size||(*cursor&15)||*cursor<original_size||
     *cursor>padded) {
    patch_reason="invalid_trampoline_cursor";return 0;
  }
  uint32_t needed=original?32:16;
  if(padded-*cursor<needed){
    patch_reason="insufficient_executable_trampoline_padding";return 0;
  }
  uint32_t off=*cursor,b;
  if(!branch((int64_t)off-at,&b)){
    patch_reason="short_branch_out_of_128MiB_range";
    /* A plain B veneer can forward straight to the original destination;
     * this also avoids stealing padding from neighboring entrypoints. */
    if((word(text+at)&0xfc000000)==0x14000000)
      return branch_alias_entry(text,original_size,at,target,original);
    return absolute_entry(text,original_size,padded,cursor,at,
                          target,guest,original);
  }
  uint32_t ins=word(text+at),relocated=ins;
  if(original){
    if(!relocate_one(ins,at,off+16,&relocated)){
      patch_reason="unsupported_first_instruction_relocation";
      return absolute_entry(text,original_size,padded,cursor,at,
                            target,guest,original);
    }
    uint32_t back;
    if(!branch((int64_t)at+4-(off+20),&back)){
      patch_reason="trampoline_return_short_branch_out_of_range";
      return absolute_entry(text,original_size,padded,cursor,at,
                            target,guest,original);
    }
    put(text+off+16,relocated);
    put(text+off+20,back);
    *original=guest+off+16;
  }
  put(text+off,0x58000050);  /* ldr x16, [pc,#8] */
  put(text+off+4,0xd61f0200); /* br x16 */
  put64(text+off+8,target);
  put(text+at,b);
  *cursor+=needed;
  patch_reason="OK";
  patch_method="short_branch";
  return 1;
}

/* All inline SVC #0x26 sites can tail-branch to the same nonreturning gate.
 * Unlike returning SVC wrappers, Break does not need a per-callsite return
 * trampoline. This avoids exhausting the finite text alignment padding on
 * large programs with many abort sites. The target trampoline is generated
 * by nx2_patch_gate for the first such site. Refuse a branch that cannot be
 * encoded instead of overwriting adjacent guest instructions. */
/* Only a single four-byte guest SVC is replaced, and only when its actual
 * opcode matches. Never touch a neighbor instruction, and never branch to
 * an unallocated or unreachable island. No returning SVC uses this path. */
static int patch_shared_nonreturning(unsigned char *text,uint32_t text_size,
                                    uint32_t padded_size,uint32_t site,
                                    uint32_t trampoline,uint32_t opcode){
  if(!text || (site&3) || (trampoline&15) ||
     site>text_size || text_size-site<4 ||
     trampoline<text_size || trampoline>padded_size ||
     padded_size-trampoline<16 ||
     word(text+site)!=opcode)return 0;
  uint32_t ins=0;
  if(!branch((int64_t)trampoline-site,&ins))return 0;
  put(text+site,ins);
  return 1;
}
int nx2_patch_shared_break(unsigned char *text,uint32_t text_size,
                           uint32_t padded_size,uint32_t site,
                           uint32_t trampoline){
  return patch_shared_nonreturning(text,text_size,padded_size,site,
                                   trampoline,0xd40004c1U);
}
/* ExitProcess is also nonreturning. A guest's direct inline SVC #7 must
 * never be allowed to terminate the host process without the guest-exit
 * handler first recording why the application stopped. */
int nx2_patch_shared_exit(unsigned char *text,uint32_t text_size,
                          uint32_t padded_size,uint32_t site,
                          uint32_t trampoline){
  return patch_shared_nonreturning(text,text_size,padded_size,site,
                                   trampoline,0xd40000e1U);
}
