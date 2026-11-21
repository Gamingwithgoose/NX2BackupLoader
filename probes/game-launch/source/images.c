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
  (void)launch;
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
    /* Service compatibility is selected by resolved SDK symbol,
     * not by title ID, game name, container format or known title list. */
    uintptr_t original = 0;
    int preserve = p->original_field != UINT64_MAX;
    if (!gate(f, m, &cursor, (uint32_t)p->offset, p->gate,
              preserve ? &original : 0, p->label)) {
      /* This one experimental compatibility hook must never turn a
       * previously launchable title into a preflight rejection if its SDK
       * entry point is not safely patchable. Keep the original path. */
      if (p->gate == (uintptr_t)nx2_ro_initialize_compat) {
        row(f, "RO_INIT_COMPAT_SKIPPED unsafe_or_unpatchable_prologue=1\n");
        continue;
      }
      return 0;
    }
    if (preserve) {
      if (all->deferred_count >= 16)
        return 0;
      unsigned n = all->deferred_count++;
      all->deferred[n].field = p->original_field;
      all->deferred[n].module = module;
      all->deferred[n].offset = original;
    }
  }
  row(f, "THUNK_SPACE module=%u used=%u available=%u resolved_hooks=%u\n",
      module, cursor - m->file_size[0], m->address[1] - m->file_size[0], count);
  return 1;
}
static int bind(FILE *f, Nx2Images *all, Nx2Launch *l) {
  Nx2Module *sdk=&all->modules[all->count-1];
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
    if (rc==NX2_MISSING_SYMBOL) {
      /* A compatible SDK function can be exported from an additional
       * NSO. Resolve by symbol, never by game ID or filename. */
      for (unsigned module=0;module+1<all->count;module++) {
        int alternate=api[i].data?
          nx2_module_address(&all->modules[module],api[i].name,&address):
          nx2_module_function(&all->modules[module],api[i].name,&address);
        if (alternate==NX2_OK) {
          row(f,"API_BIND_EXPORT_FALLBACK name=%s module=%u\n",
              api[i].name,module);
          rc=NX2_OK;
          break;
        }
        if (alternate!=NX2_MISSING_SYMBOL) {
          rc=alternate;
          break;
        }
      }
    }
    if (rc) {
      row(f, "API_BIND_FAILED name=%s status=%s searched_modules=%u\n",
          api[i].name,nx2_module_error(rc),all->count);
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
  if (!bind(f, all, l))
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
