/* SPDX-License-Identifier: GPL-2.0-only */
#include "resolve.h"
#include <stdio.h>
#include <string.h>
static uint32_t w(const unsigned char *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static int add(Nx2PatchSpec *out, unsigned *n, const Nx2PatchSpec *p) {
  if (*n >= NX2_RESOLVED_MAX)
    return 0;
  for (unsigned i = 0; i < *n; i++)
    if (out[i].offset == p->offset)
      return out[i].gate == p->gate;
  out[(*n)++] = *p;
  return 1;
}
/* role 0=rtld, 1=main, 2=SDK, 3=other. No title IDs, hashes or code offsets. */
int nx2_resolve_plan(Nx2Module *m, unsigned role, Nx2PatchSpec *out,
                     unsigned *count, char error[160]) {
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
    Nx2PatchSpec p = *spec;
    p.offset = p.svc_offset = off;
    p.symbol = NULL;
    int output = svc == 1 || svc == 0x1f || svc == 0x23 || svc == 0x29 ||
                 (role == 0 && svc == 6);
    if (output) {
      uint32_t store = (svc == 1 || svc == 0x29) ? 0xf9000041 : 0xb9000041;
      uint32_t prefix = (role == 0 && svc == 6) ? 0xf81f0fe1 : 0xf81f0fe0;
      if (off < 4 || off + 20 > m->file_size[0] ||
          w(m->guest + off - 4) != prefix ||
          w(m->guest + off + 4) != 0xf94003e2 ||
          w(m->guest + off + 8) != store ||
          w(m->guest + off + 12) != 0x910043ff ||
          w(m->guest + off + 16) != 0xd65f03c0) {
        snprintf(error, 160, "Unsupported SVC wrapper %x at %x", svc, off);
        return 0;
      }
      p.offset -= 4;
    } else if (svc == 0x26) {
      /* svcBreak does not resume guest execution. Different SDK/compiler
       * versions may put it inside an abort body, with no RET directly after
       * the SVC instruction. Patch ONLY the decoded SVC #0x26 instruction:
       * the existing Break gate captures the real kernel break parameters
       * and parks the faulting thread. Since it does not return, no wrapper
       * epilogue or LR fixup is required. Other SVCs keep their strict
       * wrapper checks and are not treated as inline-safe. */
    } else if (off + 8 > m->file_size[0] ||
               w(m->guest + off + 4) != 0xd65f03c0) {
      snprintf(error, 160, "Unsupported simple SVC wrapper %x at %x", svc, off);
      return 0;
    }
    if (!add(out, count, &p))
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
    if (!add(out, count, &q))
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
