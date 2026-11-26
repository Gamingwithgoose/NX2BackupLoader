# Runtime follow-up: OOT storage stall, profile routing and host resource gates

Date: 2026-11-26. SOURCE ONLY. No compiler, assembler, linker, build, packaging or native test executable was run. These changes are attempts requiring console validation.

## Evidence actually available

The only current root diagnostic bundle in `release/switch` contained these files: gameplay.log, nup.log, host-end.log, raw-exception.bin, last-launch.txt and system-error.log, all with the BackupLoaderMKW prefix. Copies are preserved in `.work/runtime-followup-1795712522/`. There are no current failing Yoshi, Bananza, Mario Tennis, Xenoblade or Legends ZA reports in that bundle.

| Evidence | What it establishes | What it does not establish |
| --- | --- | --- |
| Starfox Folder, guest 0400e88025b1a000 | Seven modules mapped; first present at 2453 ms under MKW | Full gameplay stability |
| OOT NUP, guest 0400486024dec000, run 1795712522 | Four modules mapped; first present at 4698 ms; 1704 presents before the recorded pause | Completion of the intro or gameplay stability |
| OOT FREEZE_SUMMARY | Eight-second presentation pause, no completed storage requests since progress, ROM-read request active for 8001 ms; seven IPC completions | Which part of the storage operation blocked, or a proven GPU/kernel deadlock |
| Empty FREEZE_STORAGE_PATH | The old capsule printed the RPC path field, which ROM reads do not populate; ROM reads use a file index | Missing or corrupt game files |
| NX2RAW02 record | Complete record with captured=false; no persisted CPU exception | Absence of a host/kernel/GPU fault |
| System-error v8 export | Successful ERPT queries, zero matching reports for the selected run window | Absence of an error outside that window or a crash that produced no ERPT entry |

User-reported results, not newly reproduced here: Xenoblade and Bananza hard-crash the host; Legends ZA and Mario Tennis fail at startup; Yoshi fails selecting an in-game save slot; OOT freezes in the intro; Star Fox reaches title. Bananza folder startup is very slow; NUP was not tested. Mario Tennis under Pokopia is rejected for a zero host system-resource pool against a 32 MiB NPDM declaration.

## Resource accounting and gates

`InfoType_SystemResourceSizeTotal` is a real kernel query, not an MKW/Pokopia lookup table. It describes process memory-management bookkeeping and is distinct from free application RAM. The loader previously rejected a native launch when the host total was zero and the guest declared a positive NPDM system-resource requirement. That check happened before actual guest allocations, and `nx2_release_heap_for_vm()` had a second corresponding minimum-resource gate.

Both native declaration-based vetoes are removed. The declaration is now advisory on the native path, including zero host pools. Legacy mode retains its previous gate. Query failures still stop preparation. Total and used kernel bookkeeping values are logged separately; free is calculated only when both queries succeed and used <= total. No returned quota or allocation result is fabricated. Actual MapPhysicalMemory, UnmapPhysicalMemory and SetHeapSize calls still return the kernel result. A zero-quota host can still legitimately fail a game that needs that quota; this source does not grant a running process extra kernel reservations.

Local policies still present and intentionally disclosed: the 1 GiB retained host allocator prefix; 4 GiB minimum planned guest region; 12 GiB maximum planned guest region, not a promise of available physical memory; real memory ownership/alias checks; and physical heap-growth headroom (normally 384 MiB, or the parsed universal HeapDef policy, 64 MiB in the OOT report). Removing ownership checks or freeing live allocator pages would risk host corruption. Host-specific constants were not introduced. Pool/headroom local refusals and actual kernel heap/mapping failures now survive minimal logging through the bounded nonblocking queue, with first-64/then-power-of-two sampling. Queue acceptance is never an allocation success response.

Observed MKW/OOT pool: 64 MiB configured against 48 MiB declared, so its captured freeze is not this initial resource-pool veto. Physical accounting after heap-tail release reported 9462349824 total, 1212317696 used, 8250032128 available bytes. Reserved address space is not treated as physically consumed memory.

## Save namespace correction attempt

The native FSP UID-less device-save mount and C++ device archive existence/creation helpers previously bypassed the profile route. They now use the same selected user context when one exists: the real hbloader UID storage, otherwise the first explicit SDK account UID observed. Reading the storage does not initialize the account service or consume applet preselected-user launch parameters. Explicit account-save UIDs remain authoritative. No user identity is guessed; if neither source supplies a UID, the existing legacy root remains in use and the report records unavailable selection. This fallback is an unresolved limitation to check on the user's actual hbloader.

The old game root, `.nx2-primary-*` binding and archive metadata formats remain intact. The primary bound profile retains the old root; other profiles retain `profiles/<lo16><hi16>`. No save is renamed, copied, deleted or merged. Yoshi has no special-case exemption. A real missing directory for a new secondary profile is reported as an absent archive by the existence query; permission/media/other errors propagate. This is a source-identified consistency gap, not proof of the Yoshi regression's cause.

## Bounded performance and cleanup changes

- Folder open-file LRU: 16 to 64 handles; eviction and complete close paths use the same capacity. Existing 4 x 512 KiB read windows and 8 x 32 KiB small-file cache remain bounded. This aims to reduce repeated opens; it does not establish a faster Bananza load or eliminate SD-service stalls.
- NUP child lookup: validate bytewise ordering once, binary search only strictly ordered child lists, linear fallback otherwise (including duplicates). An optional one-byte-per-directory ordering array falls back safely if allocation fails.
- NUP path lookup: 128 exact-path-checked cache entries, including misses, cleared on mount/close; long paths bypass the cache. The current serialized storage worker owns it. Future concurrent readers would need synchronization or private caches.
- NUP direct read chunk ceiling: 64 KiB to 1 MiB, with existing file-boundary, error, short-read, codec and checksum checks retained. Streaming decoder input buffers remain bounded as before. This reduces IPC overhead on large raw/frame fills; no speedup is yet measured.
- Native gameplay handoff releases the core's libounce HID service after its last menu input poll, following the reference's ownership approach. Guest SDK HID calls remain real. This is a regression-sensitive behavior change: verify Star Fox/Fast Fusion input. Existing applet teardown was not changed.

No full game extraction, data duplication, new dependency, per-title workaround, cache invalidation, save migration, or replacement of the direct workflow was introduced. Multi-update overlay composition and thread-aware RO-session rollover remain unresolved architectural differences documented in the earlier review; they were not silently declared solved.

## Diagnostic preservation

Freeze capsules (still at most four per launch) resolve ROM file indexes and record worker phase, offset, requested/completed bytes and result. NUP capsules add actual SD/decode/copy progress and inflight status, without adding success-path per-read disk logging. Snapshots are best-effort cross-thread observations, not atomic transaction histories. Worker phases: 1 request validation, 2 window selection, 3 cached copy, 4 window fill, 5 direct fallback, 7 finished, 8 folder open, 9 folder read. NUP phases: 1 seek/setup, 2 SD read, 3 Zstd, 4 copy.

Before truncating root reports, preparation copies known report files into `sdmc:/switch/BackupLoaderMKW/reports/previous-00` through `previous-15`. Up to seven files, each at most 8 MiB, with a temporary 32 KiB buffer freed before gameplay. Completed files use temporary-file + rename; originals are never removed by the archive helper. When full or on any archive error, launch continues and REPORT_ARCHIVE records -1. Move archived folders off the SD to reclaim/reset slots. A folder is a snapshot of previous root files, which can contain different game/run IDs; use each file's own selected-game and marker identity. The newest run remains in root files until the next launch archives it. Archive copies happen only during preparation, so first preparation can take longer when old logs are large.

`RUNTIME_SOURCE_REVISION version=20261126_storage_profile_pool_1` identifies the new runtime main object. Compile date/time is not a whole-repository build fingerprint. The GUI/core request format is unchanged.

## References and rollback

Reference source snapshots: nx2-flow commit `9ad10e2bebb0cf02082c413c06b0dcae25926014`, loader/runtime/main.c (advisory NPDM system-resource reporting, HID release) and svc.c (real physical mapping results); nx2-relink commit `b3e0fc4540784d45cd40dd9a97b1f4a5d0bacaad`. See `runtime-architecture-review-2026-11-26.md` and its manifest for all three repositories. Installed libounce headers confirm total/used info types 16/17 and the envGetUserIdStorage AccountUid ABI. Read-only libounce account source inspection confirms that initializing its account service can consume preselected-user launch parameters; this change avoids it. Existing attribution and licensing are retained.

The exact source-before snapshot for this follow-up is `.work/runtime-followup-1795712522/source-before/`; change-manifest.json records hashes. Restore only that snapshot's changed files, remove the two newly added report_archive source/header files, and preserve unrelated GUI/runtime work if rolling this follow-up back. Older playable OOT baseline and its user-produced binaries remain in `.work/oot-playable-1795679052/`; use it for comparison, not as a replacement for all current work.

## Validation and next hardware checks

Python pycparser grammar/source-wiring checks passed for the GUI, handoff/runtime loop, NUP/index readers, entire host source and archive helper after stubbing platform declarations and stripping preprocessing/GNU extensions. This is not C type checking, native fixture execution, linking, a build or hardware validation. Git whitespace checks passed. No new build or NRO was produced.

User builds with build.bat and copies the complete packaged switch directory. Keep all saves, metadata and reusable indexes in place. Check Star Fox and Fast Fusion first for launch/input regressions, then Yoshi save-slot selection under the same real user, OOT folder/NUP separately, and Mario Tennis under MKW/Pokopia. Inspect SAVE_PROFILE_CONTEXT, RESOURCE_CHECK_USED, GUEST_ERROR, FREEZE_STORAGE_STATE and FREEZE_NUP_STATE as applicable. Include archived reports plus current root reports after a test batch. No fix to title playability, freeze frequency or load speed has yet been verified on console.
