/* SPDX-License-Identifier: GPL-2.0-only */
#include "exception_forwarder.h"
#include <switch.h>

/* Read-only counterpart to the loader-forwarding investigation in
 * nx2-relink/fault_raw.c. Never change the discovered variable or code.
 * A matching instruction sequence is evidence, not a validated loader ABI. */
void nx2_exception_forwarder_audit(FILE *f,uint64_t expected_start){
 if(!f)return;
 uint64_t attached=0;
 Result debugger_rc=svcGetInfo(&attached,InfoType_DebuggerAttached,INVALID_HANDLE,0);
 fprintf(f,"EXCEPTION_FORWARDER_AUDIT version=1 expected_start=%016llx debugger_rc=%08x debugger_value=%llu read_only=1\n",
         (unsigned long long)expected_start,debugger_rc,(unsigned long long)attached);
 uint64_t address=0;unsigned regions=0,candidates=0,matches=0;
 const char *stopped="region_limit";
 for(;regions<1024;regions++){
  MemoryInfo mi={0};u32 page=0;
  Result rc=svcQueryMemory(&mi,&page,address);
  if(R_FAILED(rc)){stopped="query_failed";fprintf(f,"FORWARDER_QUERY_STOP result=%08x address=%016llx\n",rc,(unsigned long long)address);break;}
  if(!mi.size||mi.addr>UINT64_MAX-mi.size||mi.addr+mi.size<=address){stopped="end_of_map";break;}
  address=mi.addr+mi.size;
  if((mi.type&0xff)!=MemType_CodeStatic || mi.perm!=Perm_Rx ||
     mi.size<4096 || mi.size>1024*1024 || (mi.addr&3))continue;
  const volatile u32 *code=(const volatile u32 *)(uintptr_t)mi.addr;
  if(code[2]!=0x454d4f48U)continue; /* HOME prefix of HOMEBREW */
  candidates++;
  for(uint64_t k=0;k+3<mi.size/4 && matches<16;k++){
   u32 a=code[k],b=code[k+1],c=code[k+2],d=code[k+3];
   if((a&0x9f00001fU)!=0x90000007U || (b&0xffc003ffU)!=0xf94000e7U ||
      (c&0xff00001fU)!=0xb4000007U || d!=0xd61f00e0U)continue;
   int64_t imm=((a>>29)&3)|(((uint64_t)a>>5&0x7ffff)<<2);
   if(imm&(1<<20))imm-=1<<21;
   uint64_t pcpage=(mi.addr+k*4)&~UINT64_C(4095);
   uint64_t magnitude=(uint64_t)(imm<0?-imm:imm)*4096;
   if((imm<0 && pcpage<magnitude)||(imm>=0 && pcpage>UINT64_MAX-magnitude))continue;
   uint64_t target=imm<0?pcpage-magnitude:pcpage+magnitude;
   uint64_t low=((b>>10)&0xfff)*8ULL;
   if(target>UINT64_MAX-low)continue;
   target+=low;
   MemoryInfo variable={0};rc=svcQueryMemory(&variable,&page,target);
   int readable=!rc && (variable.perm&Perm_R) && !(target&7) && variable.size>=8 &&
                target>=variable.addr && target-variable.addr<=variable.size-8;
   uint64_t value=readable?*(const volatile uint64_t *)(uintptr_t)target:0;
   fprintf(f,"EXCEPTION_FORWARDER_PATTERN code=%016llx variable=%016llx query_rc=%08x readable=%u value=%016llx matches_current_start=%u pattern_only=1 modified=0\n",
           (unsigned long long)(mi.addr+k*4),(unsigned long long)target,rc,readable,
           (unsigned long long)value,readable && value==expected_start);
   matches++;
  }
 }
 fprintf(f,"EXCEPTION_FORWARDER_AUDIT_END regions=%u candidates=%u patterns=%u stopped=%s modified=0\n",regions,candidates,matches,stopped);
 fflush(f);
}
