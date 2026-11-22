/* SPDX-License-Identifier: GPL-2.0-only */
#include "images.h"
#include <switch2/memory/heap.h>
#include "folder.h"
#include "nso.h"
#include "nup.h"
#include <sys/stat.h>
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

/* Normal kaiHEN RO can track eight ready NROs in one client context.
 * The executing homebrew loader consumes one entry. If a guest needs more,
 * recycle the RO SESSION, not its mapped executable pages. kaiHEN's
 * roSessionClose explicitly leaves ready mappings in place.
 *
 * This is a one-way launch: hbloader's original RO override is closed and
 * an old context cannot unload its preserved modules. Only use it for
 * RO's exact TooManyNro result. No game name or module-name exceptions. */
#define NX2_RO_TOO_MANY_NRO ((7U<<9)|22U)
#define NX2_RO_MAX_ROLLOVERS 2U
static int rollover_ro_context(FILE *f,Nx2Images *all,unsigned module){
  if(all->ro_rollovers>=NX2_RO_MAX_ROLLOVERS){
    row(f,"RO_ROLLOVER_REFUSED reason=rollover_limit module=%u generation=%u\n",
        module,all->ro_rollovers);
    return 0;
  }
  Service hen={0};
  Result rc=kaihenOpenHomebrewSession(&all->ro,&hen);
  if(R_FAILED(rc)){
    row(f,"RO_ROLLOVER_PREFLIGHT step=hen_session result=%08x\n",rc);
    return 0;
  }
  Handle process=INVALID_HANDLE;
  rc=henGetProcessHandle(&hen,&process);
  serviceClose(&hen);
  if(R_FAILED(rc)||process==INVALID_HANDLE){
    row(f,"RO_ROLLOVER_PREFLIGHT step=process_handle result=%08x\n",rc);
    return 0;
  }
  u64 self_id=0,copy_id=0;
  Result a=svcGetProcessId(&self_id,CUR_PROCESS_HANDLE);
  Result b=svcGetProcessId(&copy_id,process);
  if(R_FAILED(a)||R_FAILED(b)||self_id!=copy_id){
    row(f,"RO_ROLLOVER_PREFLIGHT step=process_identity self=%08x copy=%08x match=%u\n",
        a,b,self_id==copy_id);
    svcCloseHandle(process);
    return 0;
  }
  /* Bypass the borrowed ldr:ro override, which names the FULL context. */
  Handle fresh_handle=INVALID_HANDLE;
  rc=smGetServiceOriginal(&fresh_handle,smEncodeName("ldr:ro"));
  if(R_FAILED(rc)||fresh_handle==INVALID_HANDLE){
    row(f,"RO_ROLLOVER_PREFLIGHT step=fresh_ro_service result=%08x\n",rc);
    svcCloseHandle(process);
    return 0;
  }
  Service fresh={0};
  serviceCreate(&fresh,fresh_handle);
  Handle former=all->ro.session;
  row(f,"RO_ROLLOVER_BEGIN module=%u preserved=%u old=%08x fresh=%08x generation=%u\n",
      module,module,former,fresh_handle,all->ro_rollovers+1);
  /* The RO server permits at most one initialized context per process.
   * Release its first context before initializing another one. */
  rc=svcCloseHandle(former);
  if(R_FAILED(rc)){
    row(f,"RO_ROLLOVER_PREFLIGHT step=close_old result=%08x\n",rc);
    serviceClose(&fresh);
    svcCloseHandle(process);
    return 0;
  }
  all->ro_rollovers++;
  all->ro=(Service){0};
  const u64 unused_pid=0;
  for(unsigned attempt=0;attempt<20;attempt++){
    /* Allow RO's server thread to observe the closed old session. */
    svcSleepThread(10000000ULL);
    rc=serviceDispatchIn(&fresh,4,unused_pid,
                         .in_send_pid=true,
                         .in_num_handles=1,.in_handles={process});
    if(R_SUCCEEDED(rc))break;
  }
  svcCloseHandle(process);
  if(R_FAILED(rc)){
    row(f,"RO_ROLLOVER_FATAL step=initialize_new result=%08x no_rollback=1\n",rc);
    fflush(f);fclose(f);svcExitProcess();
  }
  all->ro=fresh;
  row(f,"RO_ROLLOVER_READY generation=%u old_modules_preserved=%u "
        "one_way=1\n",all->ro_rollovers,module);
  return 1;
}

static void hash(unsigned char out[32], const void *p, size_t n) {
  sha256CalculateHash(out, p, n);
}
static int gate(FILE *f, Nx2Module *m, uint32_t *cursor, uint32_t offset,
                uintptr_t to, uintptr_t *original, const char *name) {
  uint32_t initial_instruction=0;
  if(offset<=m->file_size[0] && m->file_size[0]-offset>=4)
    initial_instruction=(uint32_t)m->guest[offset] |
                        ((uint32_t)m->guest[offset+1]<<8) |
                        ((uint32_t)m->guest[offset+2]<<16) |
                        ((uint32_t)m->guest[offset+3]<<24);
  uint32_t initial_cursor=*cursor;
  int ok = nx2_patch_gate(m->guest, m->file_size[0], m->address[1], cursor,
                          offset, to, 0, original);
  row(f, "PATCH name=%s offset=%x status=%s method=%s reason=%s text_bytes=%u padded_end=%u trampoline_offset=%u first=%08x\n",
      name, offset, ok ? "OK" : "FAILED",
      nx2_patch_method(),nx2_patch_failure_reason(),
      m->file_size[0],m->address[1],initial_cursor,
      initial_instruction);
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
  if(all->ro_rollovers){
    /* Mappings from the retired RO session cannot be safely unloaded
     * from its successor; terminate instead of freeing live pages. */
    row(f,"RO_ROLLOVER_PROCESS_EXIT generation=%u orphaned_ready_modules=1\n",
        all->ro_rollovers);
    fflush(f);fclose(f);svcExitProcess();
  }
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
    /* Log source size for every module before preparation. NUP entries
     * can be raw (Fast NUP) or compressed; both share the same NSO loader.
     * Folder launches report the equivalent size from the filesystem. */
    uint64_t source_bytes=0;
    unsigned source_codec=UINT32_MAX;
    int source_found=0;
    if(nx2_nup_active()){
      char entry[64];
      if(snprintf(entry,sizeof(entry),"exefs/%s",names[i])<(int)sizeof(entry))
        source_found=nx2_nup_file_info(entry,&source_bytes,&source_codec);
    }else{
      struct stat st;
      if(!stat(path,&st) && st.st_size>=0){
        source_bytes=(uint64_t)st.st_size;
        source_codec=0;
        source_found=1;
      }
    }
    row(f,"EXEFS_SOURCE module=%s type=%s found=%u bytes=%llu codec=%u\n",
        names[i],nx2_nup_active()?"nup":"folder",source_found,
        (unsigned long long)source_bytes,source_codec);
    /* The native RO module mapping retains each prior image's backing
     * pages. Read the allocator once before preparing the next NSO so a
     * genuine capacity failure is distinct from bad NUP compression. */
    row(f,"EXEFS_HOST_HEAP module=%s total=%llu free=%llu\n",
        names[i],
        (unsigned long long)heapGetTotalSize(),
        (unsigned long long)heapGetFreeSize());
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
    if(rc==NX2_RO_TOO_MANY_NRO && rollover_ro_context(f,all,i)){
      im->mapped=0;
      rc=serviceDispatchInOut(&all->ro,0,input,im->mapped,
                              .in_send_pid=true);
      row(f,"MAP_RETRY_RO_ROLLOVER module=%s generation=%u result=%08x address=%016llx\n",
          names[i],all->ro_rollovers,rc,(unsigned long long)im->mapped);
    }
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
