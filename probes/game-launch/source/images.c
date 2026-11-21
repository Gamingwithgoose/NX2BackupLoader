/* SPDX-License-Identifier: GPL-2.0-only */
#include "images.h"
#include "folder.h"
#include "nso.h"
#include "patch.h"
#include "patch_plan.h"
#include "resolve.h"
#include <dirent.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
static void row(FILE *f, const char *fmt, ...) {
  va_list a;
  va_start(a, fmt);
  vfprintf(f, fmt, a);
  va_end(a);
  fflush(f);
}
/* Written by the OOT-only AArch64 trampoline, read when the SDK aborts.
 * Sentinel means that the comparison was never reached. */
volatile uint64_t nx2_oot_budget_capture[2] = {UINT64_MAX, UINT64_MAX};
static uint32_t read_ins(const unsigned char *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void hash(unsigned char out[32], const void *p, size_t n) {
  sha256CalculateHash(out, p, n);
}
static int gate(FILE *f, Nx2Module *m, uint32_t *cursor, uint32_t offset,
                uintptr_t to, uintptr_t *original, const char *name) {
  int ok = nx2_patch_gate(m->guest, m->file_size[0], m->address[1], cursor,
                          offset, to, 0, original);
  row(f, "PATCH name=%s offset=%x status=%s\n", name, offset,
      ok ? "OK" : "FAILED");
  return ok;
}
static int apply_plan(FILE *f, Nx2Images *all, Nx2Module *m, unsigned module,
                      unsigned role, int game, const Nx2Launch *launch) {
  if (!game)
    return 0;
  Nx2PatchSpec plan[NX2_RESOLVED_MAX];
  unsigned count = 0;
  char error[160];
  if (!nx2_resolve_plan(m, role, plan, &count, error)) {
    row(f, "RESOLVE_REFUSED module=%u reason=%s\n", module, error);
    return 0;
  }
  uint32_t cursor = (m->file_size[0] + 15) & ~15U;
  for (unsigned i = 0; i < count; i++) {
    Nx2PatchSpec *p = &plan[i];
    /* Controlled A/B for Kirby Global Test Ride only. Do not change
     * working Yoshi or later-stage Zelda RO behavior by accident. */
    if (p->gate == (uintptr_t)nx2_ro_initialize_compat &&
        (!launch || launch->guest_program_id != UINT64_C(0x04005b902517c000)))
      continue;
    uintptr_t original = 0;
    int preserve = p->original_field != UINT64_MAX;
    if (!gate(f, m, &cursor, (uint32_t)p->offset, p->gate,
              preserve ? &original : 0, p->label))
      return 0;
    if (preserve) {
      if (all->deferred_count >= 16)
        return 0;
      unsigned n = all->deferred_count++;
      all->deferred[n].field = p->original_field;
      all->deferred[n].module = module;
      all->deferred[n].offset = original;
    }
  }
  /* CONTROLLED OOT LOAD TEST: do not patch the title's CMP/B.LS
   * at main+0x10ADBC8. The original budget trampoline is diagnostic,
   * NOT required by the universal heap/VM fixes. Run the game with the
   * original branch intact to rule out intrusive probe interference. */
  if(role==1 && launch &&
     launch->guest_program_id==UINT64_C(0x0400486024dec000)){
    const uint32_t site=0x10adbc8U;
    uint32_t instruction=site+8<=m->file_size[0]?
                         read_ins(m->guest+site):0;
    uint32_t branch=site+8<=m->file_size[0]?
                    read_ins(m->guest+site+4):0;
    row(f,"OOT_BUDGET_PROBE DISABLED_CONTROL_TEST original_cmp=%08x original_branch=%08x expected_original=%u\n",
        instruction,branch,
        instruction==0xeb08001fU&&branch==0x54000369U);
  }
  row(f, "THUNK_SPACE module=%u used=%u available=%u resolved_hooks=%u\n",
      module, cursor - m->file_size[0], m->address[1] - m->file_size[0], count);
  return 1;
}
static int bind(FILE *f, Nx2Module *sdk, Nx2Launch *l) {
  static const struct {
    const char *name;
    size_t field;
    int data;
  } api[] = {
#define A(field, name) {name, offsetof(Nx2LaunchApi, field), 0}
#define D(field, name) {name, offsetof(Nx2LaunchApi, field), 1}
      A(os_init, "nnosInitialize"),
      A(set_fs_allocator, "_ZN2nn2fs12SetAllocatorEPFPvmEPFvS1_mE"),
      A(fs_init, "_ZN2nn2fs6detail27InitializeFileSystemLibraryEv"),
      A(mount_rom, "_ZN2nn2fs6detail18MountRomOnFileImplEPKcONSt3__110unique_"
                   "ptrINS0_8IStorageENS4_14default_deleteIS6_EEEEPvm"),
      A(query_cache,
        "_ZN2nn2fs6detail32QueryMountRomOnFileCacheSizeImplEPmPNS0_8IStorageE"),
      A(register_fs, "_ZN2nn2fs3fsa8RegisterEPKcONSt3__110unique_ptrINS1_"
                     "11IFileSystemENS4_14default_deleteIS6_EEEE"),
      A(open_file, "_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci"),
      A(read_file,
        "_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvmRKNS0_10ReadOptionE"),
      A(write_file,
        "_ZN2nn2fs9WriteFileENS0_10FileHandleElPKvmRKNS0_11WriteOptionE"),
      A(get_size, "_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE"),
      A(close_file, "_ZN2nn2fs9CloseFileENS0_10FileHandleE"),
      A(unmount, "_ZN2nn2fs7UnmountEPKc"),
      A(create_file, "_ZN2nn2fs10CreateFileEPKcl"),
      A(delete_file, "_ZN2nn2fs10DeleteFileEPKc"),
      A(path_string, "_ZNK2nn2fs4Path9GetStringEv"),
      A(set_fs_result_handler, "_ZN2nn2fs32SetDefaultFsContextResultHandlerEPFN"
                               "S0_14AbortSpecifierENS_6ResultEE"),
      D(storage_rtti, "_ZTIN2nn2fs8IStorageE"),
      D(fs_rtti, "_ZTIN2nn2fs3fsa11IFileSystemE"),
      D(file_rtti, "_ZTIN2nn2fs3fsa5IFileE"),
      D(dir_rtti, "_ZTIN2nn2fs3fsa10IDirectoryE")};
  for (unsigned i = 0; i < sizeof(api) / sizeof(api[0]); i++) {
    uintptr_t address = 0;
    int rc = api[i].data ? nx2_module_address(sdk, api[i].name, &address)
                         : nx2_module_function(sdk, api[i].name, &address);
    if (rc) {
      row(f, "API_BIND_FAILED name=%s status=%s\n", api[i].name,
          nx2_module_error(rc));
      return 0;
    }
    memcpy((unsigned char *)&l->api + api[i].field, &address, sizeof(address));
  }
  return 1;
}
void nx2_images_free(FILE *f, Nx2Images *all) {
  for (unsigned n = all->count; n; n--) {
    Nx2Loaded *im = &all->images[n - 1];
    if (im->mapped) {
      const struct {
        u64 pid, address;
      } input = {0, im->mapped};
      Result rc = serviceDispatchIn(&all->ro, 1, input, .in_send_pid = true);
      row(f, "UNMAP module=%u result=%08x\n", n - 1, rc);
      if (R_FAILED(rc)) {
        row(f, "STOP_PROCESS unmap failed; memory retained\n");
        fclose(f);
        svcExitProcess();
      }
      im->mapped = 0;
    }
    nx2_free_pages(im->backing);
    im->backing = 0;
  }
  all->count = 0;
}
int nx2_images_load(FILE *f, const char *root, Nx2Images *all, Nx2Launch *l,
                    int game) {
  memset(all, 0, sizeof(*all));
  Result rc = kaihenGetRoSession(&all->ro);
  if (R_FAILED(rc)) {
    row(f, "RO_SESSION result=%08x\n", rc);
    return 0;
  }
  char names[13][16];
  unsigned module_count = 0;
  if (!nx2_folder_modules(root, names, &module_count)) {
    row(f, "MODULE_DISCOVERY_FAILED\n");
    return 0;
  }
  for (unsigned i = 0; i < module_count; i++) {
    char path[1200], error[160];
    snprintf(path, sizeof(path), "%s/exefs/%s", root, names[i]);
    Nx2Loaded *im = &all->images[i];
    all->count = i + 1;
    int ok = nx2_prepare_nso(path, NULL, hash, &im->backing, &im->image,
                             &im->bss, error);
    row(f, "PREPARE %s status=%s\n", names[i], ok ? "OK" : error);
    if (!ok)
      goto fail;
    int result =
        nx2_module_inspect(im->backing, im->image + im->bss, &all->modules[i]);
    if (result) {
      row(f, "PARSE module=%u status=%s\n", i, nx2_module_error(result));
      goto fail;
    }
    Nx2Module *m = &all->modules[i];
    unsigned role = i == 0 ? 0 : i == 1 ? 1 : i + 1 == module_count ? 2 : 3;
    /* OOT's SDK abort backtrace reaches main+0x10ADC3C. Before inserting
     * any executable diagnostic hook, verify the alleged comparison at
     * 0x10ADBC8 against this exact decompressed game image. This probe
     * only reads bytes during preparation and cannot alter guest code.
     * It is gated on the guest program ID, never on an NUP filename. */
    if (game && role == 1 &&
        l->guest_program_id == UINT64_C(0x0400486024dec000)) {
      const uint32_t at = 0x10adbc8U;
      if (m->file_size[0] < at + 20U) {
        row(f, "OOT_BUDGET_SITE unavailable text_size=%u needed=%u\n",
            m->file_size[0], at + 20U);
      } else {
        const unsigned char *p = m->guest + at - 12U;
        uint32_t w[8];
        for (unsigned n = 0; n < 8; n++)
          w[n] = (uint32_t)p[n * 4] |
                 ((uint32_t)p[n * 4 + 1] << 8) |
                 ((uint32_t)p[n * 4 + 2] << 16) |
                 ((uint32_t)p[n * 4 + 3] << 24);
        row(f, "OOT_BUDGET_SITE offset=%08x words=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
            at, w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7]);
        const unsigned cmp32 = w[3] == 0x6b08001fU;
        const unsigned cmp64 = w[3] == 0xeb08001fU;
        const unsigned conditional = (w[4] & 0xff000010U) == 0x54000000U;
        row(f, "OOT_BUDGET_SITE_COMPARE cmp_w0_w8=%u cmp_x0_x8=%u next_is_bcond=%u next_condition=%u\n",
            cmp32, cmp64, conditional, w[4] & 15U);
      }
    }
    if (!apply_plan(f, all, m, i, role, game, l))
      goto fail;
    armDCacheFlush(im->backing, im->image);
    const struct {
      u64 pid, source, size, bss, bss_size;
    } input = {0, (u64)im->backing, im->image,
               im->bss ? (u64)im->backing + im->image : 0, im->bss};
    rc = serviceDispatchInOut(&all->ro, 0, input, im->mapped,
                              .in_send_pid = true);
    row(f, "MAP %s result=%08x address=%016llx\n", names[i], rc,
        (unsigned long long)im->mapped);
    if (R_FAILED(rc)) {
      row(f, "STOP_PROCESS map failure; backing retained\n");
      fclose(f);
      svcExitProcess();
    }
    result = nx2_module_inspect((void *)im->mapped, im->image + im->bss, m);
    if (result)
      goto fail;
    armICacheInvalidate(m->guest, m->address[1]);
    for (unsigned seg = 0; seg < 3; seg++) {
      MemoryInfo mi;
      u32 page = 0;
      rc = svcQueryMemory(&mi, &page, (uintptr_t)m->guest + m->address[seg]);
      u32 want = seg == 0 ? 5 : seg == 1 ? 1 : 3;
      if (R_FAILED(rc) || mi.perm != want) {
        row(f, "BAD_PERMISSION module=%u segment=%u result=%08x perm=%u\n", i,
            seg, rc, mi.perm);
        goto fail;
      }
    }
  }
  if (!bind(f, &all->modules[module_count - 1], l))
    goto fail;
  if (game) {
    for (unsigned i = 0; i < all->deferred_count; i++) {
      uintptr_t address =
          (uintptr_t)all->modules[all->deferred[i].module].guest +
          all->deferred[i].offset;
      memcpy((unsigned char *)&l->api + all->deferred[i].field, &address,
             sizeof(address));
    }
    l->api.rtld = (void *)all->modules[0].guest;
    for (unsigned i = 0; i < module_count; i++)
      for (unsigned seg = 0; seg < 3; seg++) {
        Nx2Module *m = &all->modules[i];
        Nx2Region *r = &l->regions[l->region_count++];
        r->begin = (uintptr_t)m->guest + m->address[seg];
        r->end = (uintptr_t)m->guest +
                 (seg < 2 ? m->address[seg + 1] : m->total - 4096);
        r->type = seg < 2 ? 3 : 4;
        r->permission = seg == 0 ? 5 : seg == 1 ? 1 : 3;
      }
  }
  nx2_bridge_init(l);
  return 1;
fail:
  nx2_images_free(f, all);
  return 0;
}
