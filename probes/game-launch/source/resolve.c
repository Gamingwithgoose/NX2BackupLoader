/* SPDX-License-Identifier: GPL-2.0-only */
#include "resolve.h"
#include <stdio.h>
#include <string.h>
static uint32_t w(const unsigned char *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
/* Recognize complete output-pointer wrappers, not arbitrary instructions
 * around an SVC. All accepted forms save the same ABI argument, store the
 * expected-width x1 result, balance SP, and return. Replacing their entry
 * therefore leaves argument/result conversion to the normal C hook. */
static int output_wrapper(const Nx2Module *m,uint32_t off,uint32_t prefix,
                           uint32_t store){
  uint64_t end=m->file_size[0];
  if(off<4 || (uint64_t)off+16>end || w(m->guest+off-4)!=prefix)
    return 0;
  const unsigned char *p=m->guest+off+4;
  if((uint64_t)off+20<=end && w(p)==0xf94003e2U && w(p+4)==store &&
     w(p+8)==0x910043ffU && w(p+12)==0xd65f03c0U)return 1;
  /* ldr x2,[sp],#16; [cbz x2,ret]; str x1/w1,[x2]; ret */
  if(w(p)!=0xf84107e2U)return 0;
  if(w(p+4)==store && w(p+8)==0xd65f03c0U)return 1;
  if((uint64_t)off+20<=end && w(p+4)==0xb4000042U && w(p+8)==store &&
     w(p+12)==0xd65f03c0U)return 1;
  return 0;
}
static int simple_wrapper(const Nx2Module *m,uint32_t off){
  /* Only inert NOPs are permitted between SVC and RET. Never swallow an
   * epilogue, an output store, authentication, or another function. */
  uint64_t at=(uint64_t)off+4;
  for(unsigned n=0;n<4 && at+4<=m->file_size[0];n++,at+=4){
    uint32_t ins=w(m->guest+at);
    if(ins==0xd65f03c0U)return 1;
    if(ins!=0xd503201fU)return 0;
  }
  return 0;
}
static void wrapper_error(char error[160],const Nx2Module *m,
                           uint32_t svc,uint32_t off){
  uint32_t words[5]={0};
  for(unsigned i=0;i<5;i++)
    if((uint64_t)off+4ULL*i+4<=m->file_size[0])
      words[i]=w(m->guest+off+4*i);
  snprintf(error,160,"Unsupported SVC wrapper %x at %x prev=%08x code=%08x,%08x,%08x,%08x,%08x",
           svc,off,off>=4?w(m->guest+off-4):0,
           words[0],words[1],words[2],words[3],words[4]);
}
static int add(Nx2PatchSpec *out, unsigned *n, size_t capacity,
               const Nx2PatchSpec *p, char error[160]) {
  for (unsigned i = 0; i < *n; i++) {
    if (out[i].offset != p->offset) continue;
    if (out[i].gate == p->gate) return 1;
    snprintf(error,160,"Conflicting hooks at %llx: %s versus %s",
             (unsigned long long)p->offset,
             out[i].label ? out[i].label : "unnamed",
             p->label ? p->label : "unnamed");
    return 0;
  }
  if ((size_t)*n >= capacity) {
    snprintf(error,160,
             "Executable trampoline slots exhausted: count=%u available=%llu next=%s at %llx",
             *n,(unsigned long long)capacity,
             p->label ? p->label : "unnamed",
             (unsigned long long)p->offset);
    return 0;
  }
  out[(*n)++] = *p;
  return 1;
}
/* role 0=rtld, 1=main, 2=SDK, 3=other. No title IDs, hashes or code offsets. */
int nx2_resolve_plan(Nx2Module *m, unsigned role, Nx2PatchSpec *out,
                     size_t capacity, unsigned *count, char error[160]) {
  *count = 0;
  error[0] = 0;
  for (uint32_t off = 0; off + 4 <= m->file_size[0]; off += 4) {
    uint32_t ins = w(m->guest + off);
    if ((ins & 0xffe0001f) != 0xd4000001)
      continue;
    uint32_t svc = (ins >> 5) & 65535;
    const Nx2PatchSpec *spec = NULL;
    for (uint64_t i = 0; i < nx2_patch_spec_count; i++) {
      const Nx2PatchSpec *p = &nx2_patch_specs[i];
      if (p->svc == svc &&
          ((role != 0 && p->module == 5) || (role == 0 && p->module == 0))) {
        spec = p;
        break;
      }
    }
    int critical = svc == 1 || svc == 7 || svc == 0x16 || svc == 0x1f ||
                   svc == 0x20 || svc == 0x21 || svc == 0x22 || svc == 0x23 ||
                   svc == 0x26 || svc == 0x27 || svc == 0x29 || svc == 0x2c ||
                   svc == 0x2d;
    /* rtld's own GetInfo is intentionally native; QueryMemory alone is bounded.
     */
    if (!spec) {
      if (critical && role != 0) {
        snprintf(error, 160, "Unsupported inline SVC %x at %x", svc, off);
        return 0;
      }
      continue;
    }
    /* A nonreturning guest svcBreak can occur inline hundreds of times in
     * one large NSO. The images loader patches ALL of them through one
     * shared executable bridge. Skip their individual plan entries here:
     * they otherwise exhaust the bounded relocation/trampoline budget.
     * The rtld keeps its existing dedicated, verified Break wrapper. */
    if (svc == 0x26 && role != 0) continue;
    /* A normal SVC #7; RET wrapper stays on the proven resolver path.
     * Any other inline nonreturning ExitProcess belongs to the shared
     * per-module exit guard below, not an unsafe simple-wrapper hook. */
    if (svc == 7 && role != 0 &&
        (off + 8 > m->file_size[0] ||
         w(m->guest + off + 4) != 0xd65f03c0U)) continue;
    Nx2PatchSpec p = *spec;
    p.offset = p.svc_offset = off;
    p.symbol = NULL;
    int output = svc == 1 || svc == 0x1f || svc == 0x23 || svc == 0x29 ||
                 (role == 0 && svc == 6);
    if (output) {
      uint32_t store = (svc == 1 || svc == 0x29) ? 0xf9000041 : 0xb9000041;
      uint32_t prefix = (role == 0 && svc == 6) ? 0xf81f0fe1 : 0xf81f0fe0;
      if (!output_wrapper(m,off,prefix,store)) {
        wrapper_error(error,m,svc,off);
        return 0;
      }
      p.offset -= 4;
    } else if (!simple_wrapper(m,off)) {
      wrapper_error(error,m,svc,off);
      return 0;
    }
    if (!add(out, count, capacity, &p, error))
      return 0;
  }
  for (uint64_t i = 0; i < nx2_patch_spec_count; i++) {
    const Nx2PatchSpec *p = &nx2_patch_specs[i];
    /* SDK exports may live in main/subsdk as well as the final SDK
     * module. Resolve hooks wherever the matching ABI is exported,
     * independent of game identity and archive format. */
    if (p->svc != UINT64_MAX || !(p->mode & 2) ||
        !((role != 0 && p->module == 5) ||
          (role == 1 && p->module == 1)))
      continue;
    const char *name = p->symbol ? p->symbol : p->label;
    uintptr_t address = 0;
    if (nx2_module_function(m, name, &address)) {
      if (role == 1 && !strcmp(name, "nnMain")) {
        snprintf(error, 160, "Missing nnMain export");
        return 0;
      }
      continue;
    }
    Nx2PatchSpec q = *p;
    q.offset = (uint32_t)(address - (uintptr_t)m->guest);
    q.symbol = NULL;
    if (!add(out, count, capacity, &q, error))
      return 0;
  }
  if (role == 1) {
    int found = 0;
    for (unsigned i = 0; i < *count; i++)
      if (out[i].original_field == offsetof(Nx2LaunchApi, original_main))
        found = 1;
    if (!found) {
      snprintf(error, 160, "Main entry could not be resolved");
      return 0;
    }
  }
  return 1;
}
