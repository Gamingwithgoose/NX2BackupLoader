# Findings — NX2BackupLoader / BackupLoaderMKW
_Last reconciled: 2026-11-21. Branch: `experimental-nup-support`. These are engineering findings, not a claim of working general game backup loading._

## Read this first

**Status:** On actual Switch 2 hardware, Yoshi and the Mysterious Book Demo reached gameplay from extracted files (333 frames recorded in earlier testing) and reached title/profile selection from a fast NUP; Ocarina of Time (OOT) loads far into startup but shows a black screen and the system closes the host program after ~7–8 seconds. General Switch 2 compatibility is **not achieved**. Switch 1 games have **not** been demonstrated running through this loader. Do not say a graphics queue finishing means a frame was presented. This document draws on repository code, README, and user-supplied logs. When older README text says no separate crash log exists, the current source is newer: it writes `SD:/BackupLoaderMKW-crash.log`.

### Status words

- **Observed:** direct hardware or log evidence.
- **Implemented:** source contains it; not necessarily validated for all games.
- **Hypothesis:** needs controlled validation.
- **Unsafe assumption:** something not established; do not build fixes on it.

## Architecture: why Mario Kart World (MKW) is involved

MKW title override gives the loader an application context and a large heap. That is a **host process**, not a newly created OOT process. The kernel still reports host program ID `0400c3f00006e000`; a specific guest `GetInfo(ProgramId)` interception reports the selected title ID for hooked SVC calls. Virtualizing one ID does **not** grant that game's process permissions, NPDM capabilities, service access, save ownership, account privileges, program state, graphics handles, applet state or resource limits.

- Launch code: `probes/game-launch/source/main.c`. It verifies MKW host ID, reads the selected game's NPDM resource request, opens the game/index, prepares modules, releases the launcher display (`nwindowReleaseVi2Layer`) and applet proxy (`appletExit`), and starts a native guest worker on another allowed CPU core. It keeps a separate host thread to handle guest SD/IPC requests and log watchdog state.
- Game modules (`rtld`, `main`, optional `subsdk*`, `sdk`) are prepared from extracted ExeFS or experimental NUP, then mapped with a kaiHEN RO session: `images.c`, `nso.c`, `resolve.c`, `patch.c`. The original `nnMain` executes. This is native in-process relocation/loading, **not** a whole independent process, official application launch, ticket installation or full OS emulator.
- Semantic resolver `resolve.c` scans AArch64 SVC wrappers and exported SDK symbols; rejects unknown critical inline SVC or incompatible wrapper patterns rather than guessing. `patch_plan.c` is a shared hook catalog, but signatures and ABI assumptions still need checking. This is one of the best foundations for **universal** compatibility.
- The bridge `bridge.c` provides ROM storage through an IStorage-like vtable, private saves via IFileSystem-like interfaces and `Nx2Request` RPCs; host `host.c` executes SD operations. Virtual game resource identity must not be confused with real host services or the host's applet.
- Boot output is an NRO via libounce / ounce-devkit, not a Switch 1 NRO. A libnx Switch 1 port would need a separately compiled target and its own compatible runtime.

### Actual heap / VM numbers (OOT hardware log 2026-11-21)

- Host override heap: **9,321,140,224 bytes**. It is not valid to use all of that as game memory and it is **not** a claim MKW provides every guest permission.
- Retained host heap: **1,142,947,840 bytes**; usable host allocator partition ~**1,075,138,560 bytes**.
- Released tail: **8,246,001,664 bytes** returned via genuine `svcSetHeapSize`, verified unmapped; not cleared, reused as an allocator block, or freed by libc.
- Kernel-reported physical budget after release: total **9,462,349,824**, used **1,212,317,696**, available **8,250,032,128** bytes.
- `vm_release.c` tests real `GetInfo`, `QueryMemory`, allocator bounds and physical budget and enables native alias/physical mapping only after checks. Original SDK physical map/unmap calls are routed to actual kernel operations.
- The guest's subsequent `SetHeapSize` request is separately checked against native host/guest bounds; prior failure `Refused legacy SetHeapSize while native virtual memory is active` was addressed by a real guarded kernel resize, not by faking success. `GUEST_HEAP_SET_RESULT` was observed with success. A heap fix is not a GPU or process-permission fix.
- Hard-coded budget policy may be inappropriate for other games; use NPDM/resource-based policy and capability checks, and log actual kernel results.

## What the tests *prove*

| Target | Format / observation | What is and is not proved |
| --- | --- | --- |
| **Yoshi and the Mysterious Book Demo** | Extracted folder: gameplay and 333 real frames in earlier hardware test. Fast NUP: title/profile selection in earlier test. | Best positive regression benchmark. Does not establish all games work, all saves work, or Switch 1 support. |
| **Zelda Ocarina of Time** (`0400486024dec000`) | Extracted OOT: ~186,587 indexed files and ~26.25 billion virtual ROM bytes, maps 4 modules; `nnMain`, ROM mount, heap, GPU queue/window, asset reads and private save registration observed. No frame presented; at ~7–8 seconds the Switch says software had to close. | The process is not merely stuck in file indexing. Cause of termination **unknown**; no CPU exception captured. NUP OOT reached equivalent startup after format-specific repairs, so packaging alone does not explain the later failure. |
| **Pokémon Legends Z-A** | Mentioned among tests, no verified working gameplay in provided evidence. | Do not claim compatible. Preserve its exact full failure logs and build SHA for a matrix. |
| **Kirby** | Mentioned among tests, no verified working gameplay in provided evidence. | Do not claim compatible. |
| **Switch 1 backups** | No confirmed successful in-process native launch via this loader. | Need a distinct Horizon/SDK/GPU/NPDM compatibility path; official Switch 2 backward compatibility does not automatically transfer to this technique. |

### OOT chronology — failure, change, result

1. **Big folder reads:** indexed **186,587** RomFS files; cached index can be reused. Virtual ROM count and bytes are index metadata, not actual SD file size.
2. **First startup stopper:** guarded legacy `SetHeapSize` refusal during `NNINIT_STARTUP`; changed to constrained real SVC and kernel accounting. Result: reached actual `GAME_NNMAIN_ENTERED`.
3. **Earlier SDK abort:** after RomFS mount, read `System/HeapSize/Product.Nin_Ounce_NVN2.HeapDef.byml.zs`, `System/RegionLangMask.txt`, `System/GameRomConfig/NormalProduct.game__RomConfig.bgyml`, then `SDK_BREAK` reason 0, caller in SDK code region + `0xBC03C`. Break investigation added caller/stack/instruction breadcrumbs. **Do not label every later crash this same break.**
4. **Identity hypothesis:** hooked guest `GetInfo(ProgramId)` returns OOT program ID; host stays MKW. Earlier traces did not prove ID was sole cause.
5. **Memory budget hypothesis:** OOT main `+0x10ADBC8` contains original `CMP x0,x8` (`eb08001f`) followed by conditional branch (`54000369`). Earlier diagnostic experimented with a trampoline; current control test **leaves original CMP/branch unmodified**. Never blindly skip a guest assertion or force a memory threshold.
6. **NUP format blockers:** NUP v1 package/NPDM validation; OOT NPDM compressed Zstandard: **1,259-byte compressed frame -> 1,828-byte `META`**. A bundled ZBIC-oriented decoder was incorrectly used for standard Zstd frames. Fixed mode selection: standard Zstd for NUP, ZBIC for NSO. NUP NPDM file / package metadata fallback and specific error messages added. OOT folder and NUP eventually reached comparable game startup. SHA-256 and bounds validation are important; don't bypass integrity to force load.
7. **Graphics visibility:** earlier `GRAPHICS_PROGRESS bootstrap=2 getproc=1470 present_lookups=2 present_entered=0 present_returned=0`. Additional NVN instrumentation found **7/7 queue initializes return true**, **1/1 window initialize returns true**, **1/1 `nvnWindowAcquireTexture` returns 0 (success for that API)**, **1/1 queue submit** and **1/1 queue finish** return. **No `nvnQueuePresentTexture` observed and zero frames**, no proof of visible output. No direct `nvservices`/VI calls in the optional graphics service breadcrumb does not prove GPU API inactivity.
8. **Audio observations:** `aud:u` session setup yielded a pair of `0xF601` connection-closed results on particular handles; subsequent audio IPC progressed. **Correlation**, not established cause. Inspect CMIF protocol / handle ownership if recurring, and compare against successful Yoshi.
9. **Save:** `SAVE_MOUNT_REGISTER_BEGIN` -> `SAVE_MOUNT_REGISTER_RETURN` -> `PRIVATE_SAVE_MOUNT name=save result=00000000` -> `SAVE_MOUNT_GATE_RETURN`. No proof the save itself kills the process just because this is the final flushed log.
10. **Termination:** generic **“The software had to be closed because an error occurred”**, no numbered error, black screen, returns to HOME. Ounceshell/kaiHEN does not expose an Atmosphère-equivalent crash report in user testing. No `STOP_PROCESS`, CPU fault, guest/host abort or deliberate exit captured in latest logs; **cannot distinguish GPU/OS termination from uncaptured CPU fault or other exits**.
11. **Independent crash log:** added `SD:/BackupLoaderMKW-crash.log` (fresh per launch). Half-second snapshots give last safe host state. In one run last snapshot ~7.265 s, `fault_state=0`, `ipc=717/717`, ~155 MB read. In later run ~6.932 s `fault_state=0` ~150 MB, no frame. RPC trace first recorded `0x080020B0` repeatedly: **loader bridge caller, not original game instruction**; `phase=1 result=002f5e02` means pending with initial placeholder, NOT an error.
12. **Guest boundary trace correction:** `storage_read` records callback return address; seen `0x0D5ECD10`, **SDK module 3 + `0x11BD10`**; this is an SDK site, not necessarily the fault. `nx2_mount_save` traces caller **`0x0BE20C14`**; `SAVE_GATE_PROGRESS phase=1,2` and register success. Final crash snapshot around **7.839 s**, ~174.9 MB read, 808/808 IPC, `fault_state=0`; gameplay log continued through `SAVE_MOUNT_GATE_RETURN`, so an earlier snapshot at phase 2 **does not** prove it died inside `register_fs`.
13. **Latest code tracing state:** `main.c` emits `DIAGNOSTIC_PROFILE version=2`, `GUEST_CALLSITE`, `GUEST_FS_READ`, `GUEST_SAVE_GATE`, `SAVE_GATE_PROGRESS`. `bridge.c` captures IStorage callback and save wrapper callsites, completion and `nnMain` return. `fault.c` has libounce exception handler copying `ThreadExceptionDump` to shared state; host prints registers if handler fires. All remain application-local and cannot guarantee a report on kernel kill.
14. **Latest user hardware log:** 2026-11-21, `BackupLoaderMKW-crash(2).log` and gameplay `...065949`. Current last known branch before these docs: `eec0ec25c3779c721a20a75e6e92fd44d4eb3bea`. Actual user test compiled and ran (because current diagnostics appeared).

### Trace interpretation and known traps

- `HOST_SNAPSHOT` is a **point-in-time sample**, not a crash frame. The last log line can be earlier than true process death.
- `phase=1` RPC is not a failed RPC; `NX2_INVALID=0x2F5E02` is an initial sentinel. `phase=2 result=0` means that traced RPC finished.
- `fault_state=0` proves only that **our handler did not capture a CPU exception**. Do not claim no CPU fault happened.
- Guest return addresses are better captured **at the C++ vtable or SDK hook boundary** than inside `nx2_rpc`; the latter only sees an internal bridge.
- Save and file callbacks can run on different threads; a single `last_*` record can be overwritten by concurrent calls. For precise causality use a bounded per-thread ring with sequence numbers and release/acquire semantics; include thread ID, timestamp, input/output, module offset and post-return marker. Beware race conditions in an overwritten ring: validate sequence before AND after reading.
- `0xBE20C14` is a raw mapped virtual return address. Normalize to the correctly mapped module and build ID before disassembling, not a fixed file offset. `0x0D5ECD10` already matches mapped SDK module + `0x11BD10`.
- `NVN_STAGE submit/finish` confirms wrappers were called/returned; does not validate shader correctness, queue execution, swapchain, or display ownership.
- Raw results like `0xF601` must be decoded in the **correct service/IPC context**, not inferred as a universal error description.
- Runtime crashes are not solved by changing archive compression once folder and NUP reach the same point.

## Universal fixes versus per-title workarounds

**Likely reusable fixes**: comprehensive SVC semantics, service-manager/CMIF handle lifecycle and identity policy, real per-guest memory/alias/physical budget accounting, correct guest/host TLS and thread separation, native graphics/display ownership, NVN2 initialization/presentation semantics, account/HID/audio service compatibility, save filesystem correctness, NPDM requirements, loader/rtld/NSO relocation/ABI and robust failure reporting.

**Game-specific data**: title ID, assets, NPDM values, discovered module symbols, OS/SDK version, heap requirements, shader specifics, optional feature support, DLC/update metadata. These belong in a declarative compatibility profile when unavoidable.

**Bad universalization**: silently returning success for all unsupported SVCs; skipping `svcBreak`; hardcoding OOT's offsets into the generic loader; disguising all services as MKW; forcing a presented frame; assuming Switch 1 binaries use NVN2; faking a successful save operation when the file is not durably written.

**Priority invariant**: fixes must be driven by a service-level or ABI-level root cause, tested on **OOT + Yoshi + one other Switch 2 title** with behavior preserved. A per-game special case is a last resort with an explanation of the missing universal semantics and a corresponding future cleanup task.

## What is not implemented / established

- No general Switch 2 app-process creation with another title's real kernel security context.
- No verified broad commercial Switch 2 compatibility; no OOT first frame, no OOT root cause.
- No confirmed independent OS-level crash monitor/debug-port capture in kaiHEN.
- No demonstrated Switch 1 game launching through this native loader.
- No complete update/DLC/patch layering, multi-program handling, complete account/save permissions, or per-title service contract.
- No reliable claim that a modern PC Switch 2 emulator exactly reproduces the physical Switch 2 host-injection scenario.
- A NUP here is an **experimental NUP v1 package**, not an encrypted NSP/XCI replacement; encrypted game installation and ticket/entitlement processing are outside the code path.

## Next engineering plan (ordered, stop on a conclusive failure)

**P0 — preserve baseline:** record `git SHA`, hardware firmware `23.0.1`, kaiHEN + ounce-devkit versions, SD/NRO hash, exact test game build IDs; keep known-good Yoshi NRO. Do not turn on many new hooks in the same comparison. Keep per-run logs (they are overwritten).

**P1 — deterministic OOT vs Yoshi differential:** instrument *the same* checkpoints and post-return markers on both. Write a little timeline per thread: `mount_save enter -> register return -> guest wrapper return -> next guest SVC -> next NVN -> first frame`. Avoid inferring cause from final flushed log.

**P2 — unhandled termination observer:** audit `nx2_exit_gate`, `nx2_break`, all SDK-level abort handlers, any guest direct/unhooked SVC (including inlined `ExitProcess`, exception/unwind, fatal service calls), thread exit and abnormal end. For actual system kills research *documented* debug/exception interfaces available to the running process, and limitations of HEN; don't assume a retail debug port is granted. If feasible run a separate privileged monitor/companion that is genuinely outside the dying process. Do not invent the required privilege or circumvent safeguards.

**P3 — symbolization:** inspect user's **own** decompressed OOT `main` and `sdk` NSO executable text with Ghidra/rizin/objdump; verify build-ID/section bases; resolve mapped return `0xBE20C14` to module and code, then analyze return and downstream calls. Compare sdk `+0x11BD10` and previously observed SDK break `+0xBC03C`. Use bounded surrounding call graph instead of patching one guess.

**P4 — system contract differential:** map MKW host vs game NPDM capabilities, resource quotas, thread core masks, service allowlists, applet/display layer state, account user, save FS, GPU driver/version and audio handles. Where *legal and available*, inspect services' message requests/results and match actual game SDK expectations.

**P5 — graphics end-to-end:** check `nvnWindowAcquireTexture` output texture index, queue commands, fences/semaphores, `nvnQueuePresentTexture`, layer ownership and presentation errors. Compare Yoshi first frame, not only function names.

**P6 — format and architecture:** improve validated NUP v1 random access, NPDM and NSO decoding, layered base/update/DLC support without bypassing checksums; separately design Switch 1 support as a distinct backend sharing game discovery, RomFS I/O, save abstractions, logs and common IPC schema. Do not label Switch 1 compatible until an actual Switch 1 title boots.

**P7 — compatibility matrix:** log per game/build/SDK, hardware status (preflight, entry, ROM mount, GPU, save, first frame, audio, input, gameplay) and classify root cause as loader/OS/service/graphics/content/game-specific. Require Yoshi regression for each general fix.

## References / key source files

- `README.md` — branch usage, NUP and historic incident chronology.
- `probes/game-launch/source/main.c` — host lifecycle, background worker, snapshots, stage, logs.
- `probes/game-launch/source/bridge.c`, `host.c`, `launch.h` — native hooks, guest-to-host RPC, file/save service.
- `probes/game-launch/source/images.c`, `patch_plan.c`, `resolve.c` — module mapping, semantic hooks, code patch planning.
- `probes/game-launch/source/vm_release.c`, `heap_plan.*` — native budget / allocator transition.
- `probes/game-launch/source/fault.c` — application-local CPU exception capture.
- `probes/game-launch/source/nup.c`, `folder.c`, `index_cache.c` — NUP, loose assets and cache behavior.
- `probes/game-launch/Makefile`, `build.bat` — native Switch 2 toolchain / user build flow.
- Current reputable ecosystem references: https://github.com/ouncebrew/ounce-devkit ; https://github.com/ouncebrew/kaihen ; https://docs.ryujinx.app/ ; https://github.com/pound-emu/pound ; https://github.com/notsapinho/Eglantine . Third-party emulators are reference implementations, **not** evidence they can launch a specific physical Switch 2 game in this host-loader environment.

## Test hygiene

- Use only the user's legally obtained dumped files; never commit keys, console identifiers, ticket data, copyrighted game binaries, firmware or private saves to a public repo.
- Preserve real errors rather than masking them; one hypothesis per controlled experiment; log build ID and all relevant source revisions.
- No GitHub Actions or pull requests. User pulls with GitHub Desktop and runs local `build.bat`. Prefer real fixes + reproducible single-drop tests, not documentation-only changes when user asks for functionality.
- Back up SD card and saves. Running an experimental native loader inside a retail title override risks crashes, corrupt saves, and unexpected kernel behavior.

## Known reporting limitations

This is the **best reconstruction from source and supplied logs**, not a substitute for all historical artifacts. Some old experiment logs and full hashes are not available. Document additional old logs with source filenames and exact build SHAs as they become accessible.

## 2026-11-21 — Shared guest FSP domain virtualization (hardware test pending)

- **Reason:** Yoshi Full Fast NUP rendered/profile-selected, then the old FSP allowlist rejected a domain `IFileSystemProxy::OpenSaveDataFileSystem` request (command 51, object 0xc8). Yoshi Demo Fast NUP successfully completed a level and made three save/commit sequences. Minecraft separately verified three cache-storage creations and a cache mount.
- **Implementation:** `guest_fsp_vfs.c/.h` intercepts supported native guest FSP save opens at the **interface**, not by title ID. It returns a guest-local CMIF domain object backed by a per-title/per-account/per-save-attribute private SD namespace, and handles common `IFileSystem`, `IFile`, and `IDirectory` commands through the existing `RPC_SAVE_*` / directory backend. Domain object closes release host resources; cloned FSP sessions can resolve the same virtual object ID.
- **Safety:** Guest save operations are **not forwarded** to the host Mario Kart World save identity. Guest paths remain validated under the selected title's `/switch/BackupLoaderMKW/<save-key>` directory. Valid but unimplemented FSP methods now receive a real `NX2_FS_UNSUPPORTED` CMIF error instead of a hard process stop; malformed requests still stop. No per-game name, title ID, or NUP format checks.
- **Scope limitation:** This is a first, real **shared FSP compatibility subsystem**, not a guarantee of universal game launch or complete FS protocol emulation. Native FSP interfaces beyond implemented save open (51/52/53) and not-yet-implemented file operations still need semantics. FSP cloned sessions, non-domain variants, context ABI, and new hardware behavior remain to be validated. Save namespaces are separate from the legacy SDK-level mounted save root and should be reconciled deliberately when their semantics are confirmed.
- **Validation:** Source-level router/host/hook connectivity and synthetic domain CMIF response layouts were checked. No ounce-devkit cross-build or real console run is available here. Pull the branch and run local `build.bat` without GitHub Actions. Test Yoshi Demo Fast NUP first (profile, level, save), then Yoshi Full profile selection (expect `GUEST_FSP_SAVE_OPEN` / `GUEST_VFS_PREPARE` instead of `Blocked unapproved native FSP request`), then Minecraft Fast NUP.

## 2026-11-21 — Fast NUP startup/read-ahead optimization experiment

- Yoshi Demo Fast NUP save load **works** after a perceived stall. User measured **28.53 s** from launcher selection to white loading screen and **27.56 s** from save selection to gameplay. Baseline `BackupLoaderMKW-nup(20261121-194855).log` and `...crash(...194855).log` (user attachments) show 147 RomFS files, **0 ms RomFS indexing**, 3 rendered frames at 10–20 seconds after guest execution while RomFS reads rise from 112,827,865 to 409,613,082 bytes; about 550,271,279 total RomFS bytes at 61 seconds, and `GUEST_FS_READ entries=4467 returned=4467` with no SDK abort.
- The existing raw Fast NUP reader performs `nup_seek_file`+`fread` per guest read, even for contiguous raw blocks. Implemented **game-independent, bounded adaptive raw-entry read-ahead** in `nup.c`: two 1 MiB windows (2 MiB max), trigger only on observed sequential reads <=512 KiB, copy from cache on subsequent hits, preserve direct read path for random/large requests, invalidate on failed refill, free windows on NUP close. Zstd compression, extracted-folder loading, save path, and NUP bytes are unchanged.
- Added `NUP_RAW_READ_AHEAD hits=... prefetch_reads=... direct_reads=... prefetch_bytes=...` to existing launch heartbeat and `PRELAUNCH_TIMING` phases for content readiness, module preparation, and guest-worker startup. These are necessary to distinguish actual speedups from disk/card, guest CPU, and ExeFS module decompression costs.
- Tested a **portable local C equivalent** of the sequential raw-reader algorithm with 64 consecutive 64 KiB chunks: all output bytes matched, 59 cache hits, 4 prefetched windows, 1 direct read. This is NOT an actual ounce-devkit build or Switch 2 benchmark. Run `build.bat` locally and compare the same Yoshi Demo save-loading sequence before/after, while retaining Nintendo native crash logs. No claim of a particular time reduction until real hardware measurements exist.

## 2026-11-21 — Rollback of speculative Fast NUP read-ahead after Yoshi demo regression

- **User observed two repeatable visual freezes** in Yoshi Demo Fast NUP after adaptive raw-archive read-ahead landed: once after pressing A at title screen, then after loading a save and reaching gameplay. This is a behavioral regression compared with the previously successful level completion/save/reload on the same Fast NUP.
- The second failed-feeling run (20261121-202643) reported at ~73.1 seconds: `fault_state=0`, `guest_abort=0`, `host_abort=0`, `frames=2033`, `rom_bytes=1216183808`, IPC 66602/66602; even after the apparent freeze, rendering and asset traffic continued. At ~73 seconds `NUP_RAW_READ_AHEAD hits=3871 prefetch_reads=1047 direct_reads=1750 prefetch_bytes=1097181210`. No fatal read result is shown. These observations do **not** prove that read-ahead caused the visual stall, but two temporal regressions justify a controlled rollback.
- **Code change:** restored `probes/game-launch/source/nup.c` and `nup.h` byte-for-byte to the known-working commit `d90d22e343491944f9e51cc26d8087e51bd5b9d0`, eliminating the speculative buffer and associated cache metrics. Removed only the raw-cache-stat heartbeat block from `main.c`. Kept all `PRELAUNCH_TIMING` instrumentation, crash logs, existing FSP router, game-agnostic save handling, and unchanged direct file reader. Static equality checks passed; native hardware compile/run is pending.
- **Next test:** pull updated `experimental-nup-support`, rebuild via local `build.bat`, and test Yoshi Demo title screen A, save load, then gameplay responsiveness. Preserve NUP and crash logs on failure and compare frame/RomFS counters. A successful run would implicate the optimization, not conclusively prove causation. Future speedups should focus on measured bottlenecks with opt-in or rigorously regression-tested read coalescing.
