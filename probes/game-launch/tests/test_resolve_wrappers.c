/* SPDX-License-Identifier: GPL-2.0-only */
/* Synthetic ABI wrappers: accepted variants and malformed near matches. */
#include "resolve.h"
#include "native_compat.h"
uint32_t nx2_native_compat;
#include <assert.h>
#include <stdio.h>
#include <string.h>
const Nx2PatchSpec nx2_patch_specs[]={
 {.module=5,.svc=0x29,.gate=0x1000,.original_field=UINT64_MAX,.label="GetInfo"},
 {.module=5,.svc=0x1f,.gate=0x2000,.original_field=UINT64_MAX,.label="Connect"},
 {.module=5,.svc=0x21,.gate=0x3000,.original_field=UINT64_MAX,.label="Sync"}
};
const uint64_t nx2_patch_spec_count=3;
int nx2_module_function(Nx2Module *m,const char *n,uintptr_t *a){
 (void)m;(void)n;(void)a;return 1;
}
static void check(const uint32_t *code,size_t n,int accepted,uint64_t at){
 Nx2Module m={0};Nx2PatchSpec out[8];unsigned count=0;char error[160];
 m.guest=(unsigned char *)code;m.file_size[0]=(uint32_t)(n*4);
 int ok=nx2_resolve_plan(&m,3,out,8,&count,error);
 if(ok!=accepted){fprintf(stderr,"Unexpected resolve result: %s\n",error);assert(0);}
 if(ok)assert(count==1 && out[0].offset==at);
 else assert(strstr(error,"Unsupported SVC wrapper"));
}
int main(void){
 uint32_t legacy[]={0xf81f0fe0,0xd4000521,0xf94003e2,0xf9000041,0x910043ff,0xd65f03c0};
 uint32_t compact[]={0xf81f0fe0,0xd4000521,0xf84107e2,0xf9000041,0xd65f03c0};
 uint32_t nullable[]={0xf81f0fe0,0xd4000521,0xf84107e2,0xb4000042,0xf9000041,0xd65f03c0};
 uint32_t narrow[]={0xf81f0fe0,0xd40003e1,0xf84107e2,0xb4000042,0xb9000041,0xd65f03c0};
 uint32_t simple[]={0xd4000421,0xd503201f,0xd65f03c0};
 check(legacy,6,1,0);check(compact,5,1,0);check(nullable,6,1,0);
 check(narrow,6,1,0);check(simple,3,1,0);
 /* Wrong output width, wrong source pointer, unbalanced stack, branch into
  * the store rather than RET, and truncated tail must remain rejected. */
 compact[3]=0xb9000041;check(compact,5,0,0);compact[3]=0xf9000041;
 compact[0]=0xf81f0fe1;check(compact,5,0,0);compact[0]=0xf81f0fe0;
 compact[2]=0xf84087e2;check(compact,5,0,0);
 nullable[3]=0xb4000022;check(nullable,6,0,0);
 check(legacy,5,0,0);
 simple[1]=0x910043ff;check(simple,3,0,0);
 puts("SVC wrapper ABI variants: PASS");
 return 0;
}
