/* SPDX-License-Identifier: GPL-2.0-only */
#include "fault.h"
#include "folder.h"
#include "heap_plan.h"
#include "index_cache.h"
#include "nup.h"
#include "host.h"
#include "images.h"
#include "raw_svc.h"
#include "vm_release.h"
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <switch2/memory/heap.h>
static PadState pad;
static uint32_t selected_resource;
static char selected_save[256];
static char outcome[192] = "Mario Kart World override required.";
static void row(FILE *f, const char *fmt, ...) {
  va_list a;
  va_start(a, fmt);
  vfprintf(f, fmt, a);
  va_end(a);
  fflush(f);
}
static void screen(const char *message) {
  consoleClear();
  printf("BackupLoaderMKW 0.14\n\n%s\n", message);
  consoleUpdate(NULL);
}
static void __attribute__((noreturn)) stop(FILE *f, const char *why) {
  row(f, "STOP_PROCESS reason=%s\n", why);
  fclose(f);
  svcExitProcess();
}
static void game_entry(void *p) { nx2_game_worker(p); }
static int choose_core(FILE *f) {
  u64 allowed = 0;
  Result rc = svcGetInfo(&allowed, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
  u32 current = svcGetCurrentProcessorNumber();
  if (R_SUCCEEDED(rc))
    for (int i = 0; i < 6; i++)
      if ((allowed & (1ULL << i)) && (u32)i != current) {
        row(f, "WORKER_CORE host=%u selected=%d allowed=%016llx\n", current, i,
            (unsigned long long)allowed);
        return i;
      }
  return -1;
}
static int start_worker(FILE *f, Nx2Launch *l, Thread *t, int game) {
  int core = choose_core(f);
  if (core < 0)
    return 0;
  Result rc = threadCreate(t, game_entry, l, NULL, 0x100000, 0x2c, core);
  row(f, "WORKER_CREATE game=%d result=%08x\n", game, rc);
  if (R_FAILED(rc))
    return 0;
  l->worker_handle = t->handle;
  rc = threadStart(t);
  row(f, "WORKER_START game=%d handle=%08x result=%08x\n", game, t->handle, rc);
  if (R_FAILED(rc)) {
    threadClose(t);
    return 0;
  }
  return 1;
}
static void check_fault(FILE *f, const Nx2Images *images) {
  if (__atomic_load_n(&nx2_fault.state, __ATOMIC_ACQUIRE) != 2)
    return;
  const ThreadExceptionDump *d = &nx2_fault.dump;
  extern void _start(void);
  row(f,
      "NATIVE_CPU_FAULT description=%08x pc=%016llx lr=%016llx sp=%016llx "
      "fp=%016llx esr=%08x far=%016llx pstate=%08x NRO_BASE=%016llx TP=%016llx "
      "TLR=%016llx\n",
      d->error_desc, (unsigned long long)d->pc.x, (unsigned long long)d->lr.x,
      (unsigned long long)d->sp.x, (unsigned long long)d->fp.x, d->esr,
      (unsigned long long)d->far.x, d->pstate,
      (unsigned long long)(uintptr_t)_start,
      (unsigned long long)nx2_fault.tpidr, (unsigned long long)nx2_fault.tlr);
  for (unsigned i = 0; i < 29; i++)
    row(f, "FAULT_REGISTER x%u=%016llx\n", i,
        (unsigned long long)d->cpu_gprs[i].x);
  if (images)
    for (unsigned i = 0; i < images->count; i++) {
      const Nx2Module *m = &images->modules[i];
      uint64_t begin = (uintptr_t)m->guest, end = begin + m->total - 4096;
      if (d->pc.x >= begin && d->pc.x < end)
        row(f, "FAULT_PC_MODULE module=%u offset=%llx\n", i,
            (unsigned long long)(d->pc.x - begin));
      if (d->lr.x >= begin && d->lr.x < end)
        row(f, "FAULT_LR_MODULE module=%u offset=%llx\n", i,
            (unsigned long long)(d->lr.x - begin));
    }
  stop(f, "CPU fault captured; faulted thread remains parked; process exiting");
}
static int prepare_phase(FILE *f, const char *root, Nx2Images *images,
                         Nx2Launch *l, Nx2Host *host, const void *map,
                         size_t bytes, int game, uint64_t guest_id) {
  memset(l, 0, sizeof(*l));
  l->guest_program_id = guest_id;
  if (!nx2_rom_view(map, bytes, &l->rom))
    return 0;
  l->preflight_result = l->save_result = NX2_INVALID;
  for (unsigned i = 0; i < 3; i++)
    l->sample_results[i] = NX2_INVALID;
  l->arena_capacity = 0x4000000;
  l->arena_base = (uintptr_t)memalign(4096, l->arena_capacity);
  if (!l->arena_base)
    return 0;
  if (!nx2_host_init(host, f, root, selected_save)) {
    free((void *)(uintptr_t)l->arena_base);
    l->arena_base = 0;
    return 0;
  }
  if (game) {
    const Nx2HeapPlan *p = &nx2_process_heap;
    row(f, "HBL_HEAP_OVERRIDE address=%016llx size=%llu\n",
        (unsigned long long)p->original_begin,
        (unsigned long long)p->original_bytes);
    row(f,
        "STARTUP_HEAP_PARTITION status=%u host_base=%016llx host_bytes=%llu "
        "host_allocator_total=%llu host_allocator_free=%llu\n",
        nx2_process_heap_status, (unsigned long long)p->host_begin,
        (unsigned long long)p->host_bytes,
        (unsigned long long)heapGetTotalSize(),
        (unsigned long long)heapGetFreeSize());
    if (nx2_process_heap_status != NX2_HEAP_READY) {
      row(f, "GAME_HEAP_UNAVAILABLE: startup reservation requires at least 1 "
             "GiB host plus 4 GiB game memory and alignment.\n");
      nx2_host_close(host);
      free((void *)(uintptr_t)l->arena_base);
      l->arena_base = 0;
      return 0;
    }
    row(f,
        "NATIVE_VM_RESERVATION release_candidate=%016llx bytes=%llu: reserved "
        "tail will return to kernel, not be zeroed.\n",
        (unsigned long long)p->game_begin, (unsigned long long)p->game_bytes);
  }

  if (!nx2_images_load(f, root, images, l, game)) {
    nx2_host_close(host);
    free((void *)(uintptr_t)l->arena_base);
    l->heap_base = l->arena_base = 0;
    return 0;
  }
  if (game) {
    screen("Returning unused heap memory for native game allocations...");
    if (!nx2_release_heap_for_vm(f, &nx2_process_heap, selected_resource, 1,
                                 NX2_GAME_MIN_BYTES, &l->heap_capacity))
      stop(f, "Native VM memory transition refused/failed; game not started");
    l->heap_base = 0;
  }
  return 1;
}
static int scan_cancelled;
static int scan_cancel(void *unused) {
  (void)unused;
  padUpdate(&pad);
  if (padGetButtonsDown(&pad) & HidNpadButton_B)
    scan_cancelled = 1;
  return scan_cancelled;
}
static void scan_progress(uint32_t files, void *unused) {
  (void)unused;
  char message[160];
  snprintf(message, sizeof(message), "Indexing game files... %u found\nPress B to cancel", files);
  screen(message);
}
static void run(const Nx2Game *game, int rebuild_index) {
  FILE *f = fopen(game->is_nup ? "sdmc:/BackupLoaderMKW-nup.log" :
                          "sdmc:/BackupLoaderMKW-gameplay.log", "w");
  if (!f) {
    snprintf(outcome, sizeof(outcome), "Could not create launch report.");
    return;
  }
  row(f,
      "BackupLoaderMKW experimental 0.14\nSELECTED_GAME name=%s root=%s "
      "program_id=%016llx\n",
      game->name, game->root, (unsigned long long)game->id);
  u64 title = 0;
  Result rc = svcGetInfo(&title, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
  row(f, "HOST result=%08x title=%016llx\n", rc, (unsigned long long)title);
  if (R_FAILED(rc) || title != UINT64_C(0x0400c3f00006e000)) {
    row(f, "REFUSED MKW override required\n");
    fclose(f);
    snprintf(outcome, sizeof(outcome),
             "Start NX2 through Mario Kart World's override.");
    return;
  }
  if (snprintf(selected_save, sizeof(selected_save), "%s", game->name) >= (int)sizeof(selected_save)) {
    row(f, "REFUSED save name too long\n");
    fclose(f);
    return;
  }
  selected_resource = game->resource;
  if(game->is_nup) {
    screen("Opening NUP game...");
    row(f,"NUP_LAUNCH_REQUEST path=%s\n",game->root);
    if(!nx2_nup_open(game->root)) {
      row(f,"NUP_OPEN_FAILED path=%s reason=%s\n",game->root,nx2_nup_probe_error());
      fclose(f);
      snprintf(outcome,sizeof(outcome),"NUP open failed: %.115s",nx2_nup_probe_error());
      return;
    }
    size_t npdm_size=0;
    uint64_t npdm_file_size=0;
    unsigned npdm_codec=0;
    int npdm_present=nx2_nup_file_info("exefs/main.npdm",
                                       &npdm_file_size,&npdm_codec);
    uint64_t packed=0,where=0;
    nx2_nup_entry_detail("exefs/main.npdm",&packed,&where);
    row(f,"NUP_NPDM_ENTRY present=%d bytes=%llu codec=%u stored=%llu extent=%llu\n",
        npdm_present,(unsigned long long)npdm_file_size,npdm_codec,
        (unsigned long long)packed,(unsigned long long)where);
    unsigned char *npdm=nx2_nup_read_all("exefs/main.npdm",0x100000,&npdm_size);
    row(f,"NUP_NPDM_READ status=%s reason=%s\n",
        npdm?"OK":"FAILED",nx2_nup_read_error());
    if(!npdm || npdm_size<128 || memcmp(npdm,"META",4)) {
      free(npdm);
      npdm=nx2_nup_package_npdm(&npdm_size);
      row(f,"NUP_NPDM_METADATA_FALLBACK status=%s bytes=%llu\n",
          npdm?"OK":"FAILED",(unsigned long long)npdm_size);
    }
    if(!npdm || npdm_size<128 || memcmp(npdm,"META",4)) {
      row(f,"NUP_NPDM_FAILED\n");
      free(npdm);
      nx2_nup_close();fclose(f);
      snprintf(outcome,sizeof(outcome),"NUP NPDM missing or unreadable; see NUP log.");
      return;
    }
    memcpy(&selected_resource,npdm+0x14,4);
    free(npdm);
    row(f,"NUP_OPEN_OK title=%016llx resource=%u\n",
        (unsigned long long)game->id,selected_resource);
  }
  u64 resource = 0;
  rc = svcGetInfo(&resource, 16, CUR_PROCESS_HANDLE, 0);
  row(f, "RESOURCE_CHECK result=%08x available=%llu requested=%u\n", rc,
      (unsigned long long)resource, selected_resource);
  if (R_FAILED(rc) || resource < selected_resource) {
    row(f, "REFUSED insufficient host system resources\n");
    nx2_nup_close();
    fclose(f);
    snprintf(outcome, sizeof(outcome),
             "This backup requires more resources than MKW provides.");
    return;
  }
  screen(rebuild_index ? "Rebuilding game index..." : "Loading game index...");
  char rom[1400], error[160];
  if (snprintf(rom, sizeof(rom), "%s/romfs", game->root) >= (int)sizeof(rom)) {
    snprintf(outcome,sizeof(outcome),"Game path too long to construct RomFS.");
    nx2_nup_close();
    fclose(f);
    return;
  }
  if(rebuild_index && !game->is_nup)nx2_folder_part_clear(rom);
  size_t map_bytes = 0;
  void *map = rebuild_index ? NULL : nx2_cache_load(rom, &map_bytes);
  if (map) {
    row(f, "ROMFS_CACHE_HIT root=%s bytes=%llu\n",
        rom, (unsigned long long)map_bytes);
  } else {
    row(f, game->is_nup ? "NUP_INDEX_BEGIN root=%s\n" :
                            "ROMFS_SCAN_BEGIN root=%s\n", rom);
    screen(game->is_nup ? "Building RomFS map from NUP table..." :
            "Indexing extracted game files...\nPress B to save progress and cancel.");
    u64 scan_began=armGetSystemTick();
    scan_cancelled = 0;
    nx2_folder_cancel(scan_cancel, NULL);
    nx2_folder_progress(scan_progress, NULL);
    map = nx2_folder_rom(rom, &map_bytes, error);
    nx2_folder_progress(NULL, NULL);
    nx2_folder_cancel(NULL, NULL);
    row(f,"INDEX_DURATION_MS=%llu RESUMED_FILES=%u\n",
        (unsigned long long)(armTicksToNs(armGetSystemTick()-scan_began)/1000000ULL),
        nx2_folder_resumed_files());
    if (scan_cancelled) {
      row(f, "ROMFS_SCAN_CANCELLED\n");
      free(map);
      nx2_nup_close();
      fclose(f);
      snprintf(outcome, sizeof(outcome), "Indexing cancelled. Press A to restart.");
      return;
    }
    if (map && !game->is_nup) {
      if (nx2_cache_save(rom, map, map_bytes))
        row(f, "ROMFS_CACHE_SAVED bytes=%llu\n",
            (unsigned long long)map_bytes);
      else
        row(f, "ROMFS_CACHE_WRITE_FAILED\n");
    }
  }
  if (!map) {
    row(f, "ROMFS_SCAN_FAILED reason=%s\n", error);
    nx2_nup_close();
    fclose(f);
    snprintf(outcome, sizeof(outcome),
             game->is_nup ? "NUP RomFS index failed. See BackupLoaderMKW-nup.log." :
                           "Folder index failed. See BackupLoaderMKW-gameplay.log.");
    return;
  }
  Nx2RomView view;
  if (!nx2_rom_view(map, map_bytes, &view)) {
    row(f, "ROMFS_INDEX_INVALID\n");
    snprintf(outcome,sizeof(outcome),"Game RomFS metadata failed validation.");
    free(map);
    nx2_nup_close();
    fclose(f);
    return;
  }
  row(f, game->is_nup ? "NUP_ROMFS_INDEX_BUILT files=%u bytes=%llu virtual_bytes=%llu\n" :
                         "ROMFS_INDEX_BUILT files=%u bytes=%llu virtual_bytes=%llu\n",
      view.count, (unsigned long long)map_bytes,
      (unsigned long long)view.total);
  Nx2Launch *l = calloc(1, sizeof(*l));
  Nx2Host *host = calloc(1, sizeof(*host));
  Nx2Images *images = calloc(1, sizeof(*images));
  if (!l || !host || !images) {
    snprintf(outcome, sizeof(outcome),
             "Not enough memory to prepare this backup.");
    goto done;
  }
  screen("Preparing the selected game...");
  if (!prepare_phase(f, game->root, images, l, host, map, map_bytes, 1, game->id)) {
    row(f, "LAUNCH_PREPARATION_REFUSED\n");
    snprintf(outcome, sizeof(outcome),
             game->is_nup ? "NUP preparation failed; see BackupLoaderMKW-nup.log." :
                            "Game preparation failed; see BackupLoaderMKW-gameplay.log.");
    goto done;
  }
  row(f, "ACTUAL_ENTRY_ABOUT_TO_EXECUTE rtld=%016llx physical_budget=%llu\n",
      (unsigned long long)(uintptr_t)l->api.rtld,
      (unsigned long long)l->heap_capacity);
  screen("Starting game...");
  consoleExit(NULL);
  Result display_release = nwindowReleaseVi2Layer(nwindowGetDefault());
  row(f, "HOST_DISPLAY_RELEASE result=%08x\n", display_release);
  if (R_FAILED(display_release))
    stop(f, "Could not release launcher display");
  appletExit();
  row(f, "HOST_APPLET_PROXY_RELEASED\nGAMEPLAY_CUTOFF disabled\n");
  Thread worker;
  if (!start_worker(f, l, &worker, 1))
    stop(f, "Game worker could not start");
  u64 began = armGetSystemTick(), previous_frames = 0, heartbeat = began;
  u64 trace_sample = began;
  u32 previous_stage = UINT32_MAX, previous_trace_end = UINT32_MAX, previous_trace_begin = UINT32_MAX;
  for (;;) {
    check_fault(f, images);
    nx2_host_poll(host, l);
    if (host->abort || __atomic_load_n(&l->abort_requested, __ATOMIC_ACQUIRE))
      stop(f, "Native launch requested stop");
    u32 stage = __atomic_load_n(&l->stage, __ATOMIC_ACQUIRE);
    if (stage != previous_stage) {
      row(f, "LAUNCH_STAGE %u\n", stage);
      previous_stage = stage;
    }
    u64 frames = __atomic_load_n(&l->frames, __ATOMIC_ACQUIRE);
    if (frames && (!previous_frames || frames - previous_frames >= 300)) {
      row(f, "GRAPHICS_PRESENT_RETURNED calls=%llu\n",
          (unsigned long long)frames);
      previous_frames = frames;
    }
    /* Capture recent guest SVCs from shared memory without generating
     * any guest RPCs. This survives a guest thread stuck inside the SVC.
     * A short interval is used only after private-save startup. */
    if(__atomic_load_n(&l->late_trace_active,__ATOMIC_ACQUIRE) &&
       armTicksToNs(armGetSystemTick()-trace_sample)>=250000000ULL){
      trace_sample=armGetSystemTick();
      uint32_t next=__atomic_load_n(&l->ipc_trace_next,__ATOMIC_ACQUIRE);
      uint32_t ended=__atomic_load_n(&l->ipc_trace_ends,__ATOMIC_ACQUIRE);
      uint32_t started=__atomic_load_n(&l->ipc_trace_begins,__ATOMIC_ACQUIRE);
      if(ended!=previous_trace_end || started!=previous_trace_begin){
        row(f,"IPC_WATCHDOG started=%u ended=%u in_flight_estimate=%u next=%u frames=%llu\n",
            started,ended,started>=ended?started-ended:0,next,
            (unsigned long long)frames);
        uint32_t count=next<8?next:8;
        for(uint32_t back=count;back;back--){
          uint32_t slot=(next-back)%NX2_IPC_TRACE_SLOTS;
          Nx2IpcBreadcrumb *e=&l->ipc_breadcrumbs[slot];
          uint32_t phase=__atomic_load_n(&e->phase,__ATOMIC_ACQUIRE);
          if(phase)
            row(f,"IPC_WATCHDOG_CALL slot=%u phase=%u handle=%08llx type=%u command=%u caller=%016llx result=%08x\n",
                slot,phase,(unsigned long long)e->handle,e->request_type,
                e->command,(unsigned long long)e->caller,e->result);
        }
        previous_trace_end=ended;
        previous_trace_begin=started;
      }
    }
    /* A post-save checkpoint is independent of guest RPC progress;
     * use shorter heartbeats only after the save mount succeeds. */
    uint32_t late=__atomic_load_n(&l->late_trace_active,__ATOMIC_ACQUIRE);
    if (armTicksToNs(armGetSystemTick() - heartbeat) >=
        (late?1000000000ULL:5000000000ULL)) {
      heartbeat = armGetSystemTick();
      row(f,
          "LAUNCH_HEARTBEAT elapsed_seconds=%llu main_entered=%llu "
          "rpc_calls=%llu rom_bytes=%llu frames=%llu\n",
          (unsigned long long)(armTicksToNs(heartbeat - began) / 1000000000ULL),
          (unsigned long long)__atomic_load_n(&l->main_entered,
                                              __ATOMIC_ACQUIRE),
          (unsigned long long)host->calls, (unsigned long long)host->bytes,
          (unsigned long long)frames);
      unsigned pending=0;
      for(unsigned slot=0;slot<NX2_RPC_SLOTS;slot++){
        Nx2Request *request=&l->requests[slot];
        uint32_t state=__atomic_load_n(&request->state,__ATOMIC_ACQUIRE);
        if(state){
          pending++;
          /* State 1 is only claimed, so op/args are not initialized yet.
           * State 2 is published and safely readable via acquire. */
          if(state==2 || state==3)
            row(f,"RPC_PENDING slot=%u state=%u op=%u a=%016llx b=%016llx c=%016llx\n",
                slot,state,request->op,
                (unsigned long long)request->a,
                (unsigned long long)request->b,
                (unsigned long long)request->c);
          else
            row(f,"RPC_PENDING slot=%u state=%u unpublished=1\n",slot,state);
        }
      }
      row(f,"RPC_HEARTBEAT_SUMMARY pending=%u host_abort=%u guest_abort=%u stage=%u late_trace=%u\n",
          pending,host->abort,
          __atomic_load_n(&l->abort_requested,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->stage,__ATOMIC_ACQUIRE),late);
      row(f,"GRAPHICS_PATH bootstrap=%u getproc=%u present_lookups=%u present_entered=%u present_returned=%u\n",
          __atomic_load_n(&l->nvn_bootstrap_calls,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_getproc_calls,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_present_lookups,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_present_entered,__ATOMIC_ACQUIRE),
          __atomic_load_n(&l->nvn_present_returned,__ATOMIC_ACQUIRE));
    }
    if (!__atomic_load_n(&l->main_entered, __ATOMIC_ACQUIRE) &&
        armTicksToNs(armGetSystemTick() - began) > 300000000000ULL)
      stop(f, "Startup exceeded five minutes before main entry");
    s32 index = -1;
    rc = svcWaitSynchronization(&index, &worker.handle, 1, 0);
    if (R_SUCCEEDED(rc))
      stop(f, "Game worker returned unexpectedly");
    if (rc != KERNELRESULT(TimedOut))
      stop(f, "Game worker wait failed");
    svcSleepThread(1000000);
  }
done:
  row(f, "REPORT_END game_entry_attempted=0\n");
  fclose(f);
  nx2_nup_close();
  free(map);
  free(l);
  free(host);
  free(images);
}
static void draw(const Nx2Catalog *catalog, size_t selected) {
  consoleClear();
  printf("BackupLoaderMKW 0.14\n\n");
  if (!catalog->count)
    printf("No extracted games or NUP backups found in /games.\n\n");
  else {
    size_t page = (selected / 15) * 15;
    for (size_t i = page; i < catalog->count && i < page + 15; i++)
      printf("%s %.66s\n", i == selected ? ">" : " ", catalog->items[i].name);
    printf("\n%u / %u\n", (unsigned)selected + 1, (unsigned)catalog->count);
    if (catalog->items[selected].is_nup)
      printf("NUP: %s\n", catalog->items[selected].nup_status);
    else if (catalog->items[selected].updates)
      printf("This selection loads base; updates are not merged.\n");
  }
  printf("\nUp/Down: select   A: launch   Hold B+A: rebuild index   X: refresh   +: exit\n\n%s\n",
         outcome);
  consoleUpdate(NULL);
}
int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  consoleInit(NULL);
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad);
  Nx2Catalog catalog = {0};
  nx2_catalog("sdmc:/games", &catalog);
  size_t selected = 0;
  u64 repeat = 0;
  int direction = 0;
  while (appletMainLoop()) {
    padUpdate(&pad);
    u64 down = padGetButtonsDown(&pad), held = padGetButtons(&pad);
    if (down & HidNpadButton_Plus)
      break;
    if (down & HidNpadButton_X) {
      nx2_catalog_free(&catalog);
      nx2_catalog("sdmc:/games", &catalog);
      selected = 0;
    }
    u64 up = HidNpadButton_Up | HidNpadButton_StickLUp,
        dn = HidNpadButton_Down | HidNpadButton_StickLDown;
    int next = (held & up) ? -1 : (held & dn) ? 1 : 0;
    u64 tick = armGetSystemTick();
    int move = 0;
    if (next && ((down & (up | dn)) || next != direction)) {
      move = next;
      repeat = tick + armNsToTicks(350000000);
    } else if (next && tick >= repeat) {
      move = next;
      repeat = tick + armNsToTicks(100000000);
    }
    direction = next;
    if (catalog.count && move)
      selected = move < 0 ? (selected ? selected - 1 : catalog.count - 1)
                          : (selected + 1) % catalog.count;
    if (catalog.count && (down & HidNpadButton_A)) {
      const Nx2Game *game=&catalog.items[selected];
      if (game->is_nup == 2) {
        /* Invalid NUP selections never entered run(), so they previously
         * returned to the menu without creating any launch log. */
        FILE *rejected=fopen("sdmc:/BackupLoaderMKW-nup.log","w");
        if(rejected){
          row(rejected,"BackupLoaderMKW experimental 0.14\n"
             "NUP_CATALOG_REFUSED name=%s path=%s title=%016llx reason=%s\n",
             game->name,game->root,
             (unsigned long long)game->id,game->nup_status);
          fclose(rejected);
        }
        snprintf(outcome,sizeof(outcome),"NUP not ready: %.95s",game->nup_status);
      } else
        run(game, (held & HidNpadButton_B) != 0);
    }
    draw(&catalog, selected);
    svcSleepThread(16666667);
  }
  nx2_catalog_free(&catalog);
  consoleExit(NULL);
  return 0;
}
