/* SPDX-License-Identifier: GPL-2.0-only */
/* cc -std=c11 -O2 -Wall -Wextra -Werror -I../source
 *    test_resolve_capacity.c ../source/resolve.c -o test_resolve_capacity
 * Portable synthetic SVC wrappers, no Switch 2 hardware or game data. */
#include "resolve.h"
#include "native_compat.h"
uint32_t nx2_native_compat;
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const Nx2PatchSpec nx2_patch_specs[]={
  {.module=5,.svc=0x27,.gate=(uintptr_t)0x1000U,
   .original_field=UINT64_MAX,.label="OutputDebugString"}
};
const uint64_t nx2_patch_spec_count=1;
int nx2_module_function(Nx2Module *m,const char *name,uintptr_t *address){
  (void)m;(void)name;(void)address;return 1;
}
static void put32(unsigned char *p,uint32_t word){
  for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(word>>(i*8));
}
int main(void){
  enum{sites=180,bytes=sites*8};
  unsigned char *code=calloc(1,bytes);
  Nx2PatchSpec *plan=calloc(sites,sizeof(*plan));
  if(!code||!plan){free(code);free(plan);return 2;}
  for(unsigned i=0;i<sites;i++){
    put32(code+i*8,0xd40004e1U); /* svc #0x27 */
    put32(code+i*8+4,0xd65f03c0U); /* ret */
  }
  Nx2Module m={0};
  m.guest=code;m.file_size[0]=bytes;
  unsigned count=0;char err[160];
  int success=nx2_resolve_plan(&m,3,plan,sites,&count,err);
  int dynamic_ok=success&&count==sites&&
                 plan[sites-1].offset==8*(sites-1);
  count=0;
  int bounds_ok=!nx2_resolve_plan(&m,3,plan,128,&count,err)&&
                strstr(err,"trampoline slots exhausted")!=NULL&&count==128;
  free(code);free(plan);
  puts(dynamic_ok&&bounds_ok?"Resolver real capacity: PASS":
                             "Resolver real capacity: FAIL");
  return !(dynamic_ok&&bounds_ok);
}
