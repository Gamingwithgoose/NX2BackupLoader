# Runtime compatibility and on-console preparation review

Date: 2026-11-26. Initial architectural comparison is read-only with respect to runtime source. Later, separately described corrections are source-only and unverified on hardware. This is an architectural review, not a complete audit of every instruction, service or reference renderer.

## Reviewed revisions and evidence

| Project | Exact input |
| --- | --- |
| BackupLoaderMKW | Working source over HEAD `227a89d975be3b4362714aa78c07884b3a185a1a`; **dirty**, including unbuilt native-startup, GUI, resource-policy and reader changes. HEAD alone does not identify this source. |
| nx2-flow | [`9ad10e2bebb0cf02082c413c06b0dcae25926014`](https://github.com/Olmectron/nx2-flow/tree/9ad10e2bebb0cf02082c413c06b0dcae25926014) |
| nx2-relink | [`b3e0fc4540784d45cd40dd9a97b1f4a5d0bacaad`](https://github.com/Olmectron/nx2-relink/tree/b3e0fc4540784d45cd40dd9a97b1f4a5d0bacaad) |

The [manifest](runtime-architecture-review-2026-11-26-manifest.json) records hashes of the 87 local source inputs and 58 retrieved upstream text files. Offline references and the pre-follow-up local source snapshot are under `.work/architecture-review-20261126/`. No game assets were imported from the references. The two references share substantial runtime source; they are related implementations, not independent demonstrations of two different compatibility mechanisms.

Evidence levels used below:

- **Confirmed source difference/defect:** directly visible in the reviewed source. This does not establish the cause of a particular console failure.
- **Potential defect:** a plausible connection requiring an appropriate log, fixture or hardware test.
- **Unsupported assumption:** a conclusion for which the current evidence is insufficient.

User reports that the same backups play in the references are relevant compatibility evidence. We have not personally inspected the reference hardware runs, matched all versions/hosts/settings, or reproduced them. The latest locally available Loader Logs are still run `1795679052`, not a new GUI/resource-refusal run.

## Architecture comparison

| Area | BackupLoaderMKW | nx2-flow | nx2-relink |
| --- | --- | --- | --- |
| Entry/workflow | Folder and NUP catalog; separate GUI and generic runtime; checked one-shot request; native path runs the original guest `rtld` on the original main thread. | Separate menu and generic game loader; NUP path passed through hbloader. No per-game PC compilation. | PC prepares/compiles a game port; program modules are embedded/relinked or mapped whole. |
| Resource ownership/cleanup | UI releases framebuffer/layer, shared fonts, status/art sessions and caches before exiting. Runtime retains module backing and a 1 GiB allocator prefix. RO rollover makes some mappings one-way. | Menu exits before loader. Runtime splits an ordinary allocator from its own large-block arena and can trim unused arena tail. | Host heap includes module backing; whole images can remain duplicated. Restores the original heap extent before returning to hbloader. |
| Memory accounting | Real kernel queries plus a guest logical view informed by HeapDef; actual allocation/mapping calls remain bounded. Fixed host prefix has not been replaced by a measured arena. | Early heap release; measured arena high-water trim; HeapDef changes the reported budget. Some visible memory figures are constants/policy, not measured consumption. | Similar early release and kernel allocation path; game-specific compile-time budget choices are derived during PC preparation. |
| SDK initialization | Original `rtld`, original-main-thread kernel handle, NPDM stack/priority/core, cleared SDK TLS region. | Own linker/ModuleObject construction; SDK `nn::init::Start` variants when found; fallback initialization sequence. | Same custom runtime family; SDK start over prepared embedded/whole modules. |
| Threads/synchronization | Dedicated preparation/observer and storage threads; explicit RPC states; bounded async diagnostics. Main thread is handed to guest. | Original main thread handed to SDK; own writer/watcher; worker for compressed assets. Some optional watchers pause threads. | Same runtime family; optional watcher and synchronous waits for log durability. |
| Filesystem/cache | Resumable folder `.part`, persistent `.idx`, loose-file caches/read-ahead; NUP raw/streamed Zstd reads. Cache freshness only observes root timestamps. | Menu metadata/icon cache keyed by NUP size/mtime; runtime builds virtual RomFS tables; compressed assets are unpacked whole into a bounded cache on demand. | PC creates an image of game files, with streaming installation; runtime serves it and optional loose mods. |
| Archives | Validates TOC hash, extents, names and codecs. **Currently selects one RomFS root**; this is not a union of package histories. | NUP-only game entry; base plus updates sorted by version; virtual tables built on console. | Folder/NUP input on PC; base/update layers combined by tooling; resulting image used by runtime. |
| Metadata | Filename fallback, NACP/TLV reading, system control cache fallback. Review input assumes zero-based TLV types. | Menu detects the TLV base from a real NPDM payload; cache avoids reparsing unchanged archives. | PC tooling extracts identity; can request cached console identity for a later PC-side update. |
| Saves | Existing private roots, account/profile binding, separate cache/BCAT metadata, native SDK registration through private FSP VFS. | SDK FSP messages adapted to a private SD folder/slot, sometimes through a borrowed real SD domain session. | Same private-folder approach, organized per port; PC installer includes legacy path migrations. |
| Updates | Folder selects newest complete update, with base fallback. NUP reader exposes selected package. Neither is the full ordered multi-update union of the references. | Selects latest executable program and overlays each package's assets in version order; source currently considers at most eight base/update layers. | Tooling overlays input layers in version order, newest program selected separately from assets. |
| Diagnostics | CPU/raw exception and system error export; minimal or verbose modes. Minimal review input drops some fatal SDK records unintentionally. | Ring writer, host-end/error-report capture, prior log/crash summary, optional intrusive watchers and detailed logging. | Similar capture; persistent per-port logs. Detailed logging can wait for disk durability on the caller. |

Principal source references:

- Flow [card layout/handoff](https://github.com/Olmectron/nx2-flow/blob/9ad10e2bebb0cf02082c413c06b0dcae25926014/docs/layout.md), [loader entry and heap ownership](https://github.com/Olmectron/nx2-flow/blob/9ad10e2bebb0cf02082c413c06b0dcae25926014/loader/runtime/main.c), [on-console preparation/layers](https://github.com/Olmectron/nx2-flow/blob/9ad10e2bebb0cf02082c413c06b0dcae25926014/loader/runtime/game.c#L1145).
- Flow [module merge planner](https://github.com/Olmectron/nx2-flow/blob/9ad10e2bebb0cf02082c413c06b0dcae25926014/loader/runtime/merge.c#L174), [whole image/SVC thunks](https://github.com/Olmectron/nx2-flow/blob/9ad10e2bebb0cf02082c413c06b0dcae25926014/loader/runtime/whole.c), [custom initialization](https://github.com/Olmectron/nx2-flow/blob/9ad10e2bebb0cf02082c413c06b0dcae25926014/loader/runtime/loader.c#L782).
- Flow [save IPC](https://github.com/Olmectron/nx2-flow/blob/9ad10e2bebb0cf02082c413c06b0dcae25926014/loader/runtime/rom.c#L620), [SVC/service behavior](https://github.com/Olmectron/nx2-flow/blob/9ad10e2bebb0cf02082c413c06b0dcae25926014/loader/runtime/svc.c), [metadata interpretation](https://github.com/Olmectron/nx2-flow/blob/9ad10e2bebb0cf02082c413c06b0dcae25926014/menu/source/nup.cpp#L131).
- Relink [input/layer preparation](https://github.com/Olmectron/nx2-relink/blob/b3e0fc4540784d45cd40dd9a97b1f4a5d0bacaad/tools/game2.py), [compiler/build decisions](https://github.com/Olmectron/nx2-relink/blob/b3e0fc4540784d45cd40dd9a97b1f4a5d0bacaad/tools/build.py), [instruction rewriting](https://github.com/Olmectron/nx2-relink/blob/b3e0fc4540784d45cd40dd9a97b1f4a5d0bacaad/tools/relink.py).
- Local: `main.c`, `startup_heap.c`, `heap_config.c`, `heap_plan.c`, `images.c`, `bridge.c`, `svc_hooks.c`, `host.c`, `guest_fsp_vfs.c`, `nup.c`, `folder.c`, `index_cache.c`, and `probes/library-ui/source/`.

## Confirmed gaps and consequences

| Finding | Source evidence | Consequence / disposition |
| --- | --- | --- |
| MKW-only gate contradicts host-agnostic requirement | `main.c` rejects a queried host ID other than `0400c3f00006e000`. | Remove title identity as an eligibility gate; still require real host/resource queries. Host-agnostic does not mean every host supplies every resource. |
| Archive updates are not layered | `nup.c` stores one `romfs_dir`; `lookup_file` and `nx2_nup_romfs_walk` only use it. `folder.c` adds entries directly. | A file present in base/older update but absent from selected update is missing. Correct with an ordered manifest and winning-path overlay; do not mix executable modules across program versions. |
| Folder updates omit intermediate layers | `nx2_choose_update` selects one newest complete root plus base. | Multi-update histories with inherited assets need more than a two-root union. Whether a specific dump needs this is not yet established. |
| No on-console module merge planner | `images.c` retries an exact slot-limit rejection by closing/recreating RO contexts; reference `merge.c` preserves main/SDK/TLS modules and combines eligible others. | Different ownership, loader-slot and dynamic-RO behavior. Requires a separate reviewed module strategy, not copying a single helper into original-rtld startup. |
| Legacy TLV base mismatch | Reference menu anchors metadata types to a validated `META` NPDM payload; review input hardcodes NACP=1, ICON=2. | A legacy NPDM in type1 can become the title META; later fields can also be misidentified. Normalize a validated legacy namespace, not just suppress the visible word. |
| Essential fatal messages can be filtered | Minimal `host.c::logline` only retains FAILED/REFUSED/ERROR, while SDK fatal records use SDK_BREAK/SDK_ABORT. | Preserve fatal records and flush them even with minimal logging or exhausted routine-log quota. This is a diagnostic defect, not an established crash cause. |
| Rebuild action lost from GUI | Original B+A passed `rebuild_index=1`; GUI always requests `run_native(...,0,1)` and its Refresh only reloads the catalog. | Restore an on-console selected-index rebuild action without another launch hotkey; compatible existing indices remain intact until explicitly rebuilt. |
| Cache freshness has a known blind spot | `index_cache.c::stamp` checks root/base directory mtimes; payload checksum only checks the saved index itself. | Nested file changes can leave a structurally valid stale index. A deliberate rebuild must be available; stronger invalidation needs a cheap content/program fingerprint or an explicit rescan. |

## Potential defects needing evidence

1. **Applet/HID ownership:** ours releases the applet proxy unconditionally but does not explicitly release the runtime HID resource at that point. Reference Switch 2 code normally retains applet state (it documents a fatal close/reopen case) and releases HID. The SDKs and routing differ, and OOT did run with our existing path. Do not swap these calls blindly. Capture the actual failing applet/HID command and owner; perform one reversible handoff experiment at a time.
2. **Host-memory overhead:** ours retains a 1 GiB allocator-managed prefix; flow uses a separate arena and can trim its own tail. Free bytes inside mimalloc are not safe to return merely because they appear unused. Partition before allocation, track real ownership and backing pages, then measure physical usage and the arena high-water mark. The earlier 512 MiB attempt failed module preparation.
3. **Dynamic RO after session rollover:** retained mappings outlive the context that created them. Runtime registration/unload expectations need explicit tests. OOT's preserved run has four modules and no rollover evidence, so this cannot be asserted as its cause.
4. **SVC patch coverage/branch reach:** both readers must support actual instructions and safe ±128 MiB branch geometry. The reference has near/far thunk pools; our entry hooks have some long-range fallbacks, but the complete inline-SVC coverage is not proven equivalent. Unsupported layouts need exact module/build/site records and adversarial instruction fixtures, not guessed rewrites.
5. **Storage stalls:** the preserved OOT run has a 6.4-second texture-open interval, but that does not prove SD hardware, directory lookup, logging or the GPU is responsible. Test folder/NUP independently, with transfers stopped as the user already confirmed.

## Assumptions that must not become fixes

- A graphics counter that was not instrumented is not proof the operation never happened.
- A game reaching its menu does not verify its save, thread or gameplay-transition path.
- Host reserved address space, allocator free bytes and committed physical pages are different quantities.
- A declared NPDM reservation above a nonzero host pool is not itself a failed allocation; actual kernel refusals still matter. A missing required pool remains a real constraint.
- The reference's fixed 128 MiB visible program charge / 8992 MiB policy is not measured consumption for our runtime. Do not import these constants as facts.
- Optional-service stand-ins that return empty success, protection requests intentionally left unapplied, or RO registration replies completed locally are not universally standards-consistent fixes. Assess each real protocol/ownership issue; preserve legitimate errors.
- The related references do not demonstrate that every title works, that host selection is irrelevant, or that module merging is safe for every SDK/TLS layout.

## What can happen entirely on the console

| Work | Current reference implementation | BackupLoaderMKW direction |
| --- | --- | --- |
| Read NUP/folder manifests, NPDM, NSO headers and HeapDef | Flow does NUP preparation at runtime; relink tooling does folder/NUP preparation on PC. | Console for both formats; one content manifest shared by preparation paths. |
| Select program and overlay base/update assets | Flow builds the ordered union on console; relink does it during PC preparation. | Console; table-only virtual RomFS. No full asset duplication or required extraction. |
| Decompress LZ4/ZBIC modules; register/map images | Flow on console. | Already largely console-side; preserve and extend with fixtures. |
| Plan/merge eligible modules and patch instructions | Flow on console, within its documented slot/TLS limits; relink has a broader PC preparation path. | Feasible on console, but not yet equivalent in our original-rtld path. Reusable core plus cached plans; no individual PC game NRO. |
| Relocate, initialize SDK/TLS and start threads | Both runtimes do this on console, with different linker models. | Preserve working original-rtld baseline while testing an isolated alternative strategy. Never initialize constructors twice. |
| Metadata/art | Flow caches NUP identity/icons and uses optional covers. | Lazy reads, validated TLV namespace, cached console identity; no network required for existing metadata. |
| Save mounting | Both runtimes do it on console. | Keep our save paths, user binding and namespace metadata. Do not run reference migrations against existing saves. |
| Compile/install the generic launcher | SDK build on a development machine. | User's existing build.bat remains. This is different from per-game PC preparation. |
| A reference title beyond Flow's merge limits | Reference README currently directs the user to a PC-made relink port. | Not solved merely by copying Flow. Additional on-console linker work would be needed; no claim that a PC fallback is technically unavoidable forever. |

Reusable caches must store versioned plans/metadata, not live handles, host pointers, applied absolute relocations or SDK objects. Identify inputs with source/module identity, package selection, runtime/patcher version and schema; publish atomically. Keep compatible `.part`/`.idx` files and existing saves. A cache mismatch should rebuild only affected preparation data. No persistent full RomFS copy is needed for direct launch.

## Implementation sequence and validation gates

1. **Small confirmed corrections:** restore host-agnostic eligibility, retain fatal diagnostics, interpret validated legacy NUP facts, and restore on-console rebuild selection. Keep the same original-rtld launch path. Record changes after this read-only input snapshot.
2. **Content manifest / update union:** implement a shared ordered layer description for folder and NUP; separate latest complete executable program from file fallback. Test inherited base-only, older-update-only, overwritten, duplicate, file/directory-conflict, malformed and unrelated-title cases. Validate on hardware in both formats.
3. **Module-slot ownership:** inventory static/dynamic RO requirements and TLS; log exact slot failures. Design a distinct merge/relink strategy with explicit relocation/constructor ownership. Do not combine custom ModuleObjects with original-rtld initialization accidentally. Keep existing baseline selectable/rollbackable until hardware regression gates pass.
4. **Memory ownership:** separate loader allocator and large preparation arena before initialization; size from measured module/table requirements, avoid temporary/backing duplication, return only wholly unowned tail. Require true kernel-accounting comparisons and allocation-refusal preservation.
5. **Performance:** profile real open/read/decode latency, handle/cache hit rates and physical usage. Keep routine logging off storage hot paths; retain bounded event-triggered failure evidence. Cache unchanged metadata without rescanning game trees unnecessarily.

## Hardware regression matrix

Each cell requires a real run; source/grammar/host-interpreted arithmetic checks do not fill a gameplay result. Use the same host, firmware, kaiHEN build, game version, save and format when comparing. Record the actual build and selected content identity. Archive each run's logs before another game overwrites them.

| Title | Established baseline | Folder gate | NUP gate |
| --- | --- | --- | --- |
| Star Fox | Working; regression control | Launch, title screen, tutorial, save/reopen | Same, independently when available |
| Fast Fusion | Working; regression control | Race, audio/video, return/relaunch | Same, independently when available |
| Ocarina of Time | Reached gameplay; lag/skips and freeze | Restore first frame/gameplay; resource values; repeat former freeze scene | Same title/version/save; compare open/decode latency |
| Yoshi | Intermittent gameplay freeze | Complete stage, save, repeat freeze workload | Same, independently |
| Donkey Kong Bananza | Gameplay-transition freeze | Existing-save and new-game transition | Same, independently |
| Drag x Drive | Loading-stage freeze | Pass former loading point | Same, independently |
| Kirby Air Ride | Startup issues | Module/SVC/RO identity and first frame | Same, independently |
| Minecraft | Startup issues | Module/SVC/RO identity and first frame | Same, independently |

The user suspects the recent resource message was OOT; the affected entry is not identified in a fresh log. The preserved successful OOT log records host system pool **67,108,864 bytes** and guest request **50,331,648 bytes**. It produced **4,324 completed presents** before the observed stop in progress. These facts do not verify any newer build.

## Rollback and licensing

- Preserved playable OOT source, NRO/ELF and logs: `.work/oot-playable-1795679052/`.
- Its NRO SHA256: `cf77a1fbfd7b6cf518f9838eaf0a90141a4212bd18f75ec56c1eedaf4061f760`.
- Review-input source snapshot: `.work/architecture-review-20261126/local-before-followup/`. These `.work` snapshots are local and ignored by Git; the reference commits and hash manifest are reproducible documentation.
- No SDK builds, packaging, installs, commits or pushes were performed for this comparison. No new dependencies were installed.
- Relink and Flow's loader are MIT; Flow's menu is GPL-2.0-or-later; their third-party decoder has its own license. Existing project licenses/attribution must remain. Do not transplant a whole repository or service stubs simply because a video shows gameplay.

## Follow-up status

After freezing and documenting the read-only inputs, implemented these small source corrections:

- Removed the hardcoded MKW title-ID eligibility check. Real host identity/resource queries and allocation checks remain; unavailable/insufficient required host capabilities are still errors.
- Preserved BREAK/ABORT/FAULT/STOP diagnostics in Minimal mode, including after the routine-log quota, and flush fatal records.
- Added `nup_meta.h`: recognize zero/one-based fact tags only from a bounds-checked NPDM with ACI0 structure. Both the UI's facts and runtime's metadata NPDM fallback accept the known schemes. A legitimate name such as METAL GEAR is not treated as a binary header. Image candidates must have JPEG/PNG signatures before they suppress other artwork fallbacks.
- Restored Options > Rebuild selected folder index. It invalidates only the selected generated `.idx` and its partial checkpoints, then leaves launching to A. The UI reports failures instead of pretending deletion succeeded. NUP's archive table is not deleted, and saves/game files are never targeted. No launch-request ABI change was needed for this action.

Added adversarial legacy-metadata fixture source (`test_nup_meta.c`); it is not natively compiled/executed. C grammar checks cover the helpers, fixtures and changed runtime/UI paths. Ten large-NUP boundary assertions were earlier executed by interpreting the actual pure C policy and fixture in Python, not by compiling a host/native program. Static checks do not establish SDK type/link correctness or console compatibility.

The existing SDK archive's symbol index contains the NS and shared-font entry points used by the UI; this is a read-only availability check, not a link test. The preserved playable OOT NRO hash remains unchanged. No build, packaging, installation, commit or push occurred.

**Not implemented by these small corrections:** ordered multi-update unions, a replacement module linker/merge backend, or a separately owned large-memory arena. These remain substantial compatibility work. Applet/HID ownership, Yoshi/DK/Drag x Drive freezes and general startup failures remain unresolved. Fresh hardware logs are needed to identify the actual stopping point; none of the new source changes are described as successful gameplay fixes.
