# GameFindings — MKW-host cross-game investigation
Updated 2026-11-23. Research only: executable and build tools remain identical to preserved playable baseline 848a47dcf26fbdb8499aa2b552bfebd4fe2de170, restored to experimental-nup-support at b1f446f51919bfcb8088cc6cb2e9402aaddfed82.

## Scope and confidence vocabulary
This is a full inventory of the currently accessible ExeFS collection, MKW-host hardware observations, differences, similarities, unknowns and potential fixes. It is NOT an assertion that the root cause of any freeze is proven. The user supplied the seven-part ExeFS archive, reassembled into the nested “exefs collections.zip”; all main.npdm and NSO headers of 11 game titles and available updates were inspected directly. Hardware results are from uploaded launch/crash logs and existing append-only Findings.md and HANDOFF_2026-11-21.md. The archive is not necessarily the *same package update* as each hardware test. No game files were altered. **V** = measured directly in binaries or source; **O** = observed hardware outcome/log; **H** = unproven hypothesis; **U** = unknown/unmeasured; **R** = actionable prospective fix/test. A graphics PresentTexture return is NOT the same as visible scanout. Main and guest run in MKW's same process, not an independently launched process.

## 1. MKW-host outcomes — do not collapse distinct failure categories

| Guest | User outcome through MKW | Diagnostic category | Status |
|---|---|---|---|
| **Star Fox** | Visibly playable, sustained >23 min in known-good build | **Positive control** | O verified user gameplay |
| **Fast Fusion** | Visibly playable, no reported gameplay freezing | **Positive control** | O; precise instrumentation absent |
| **Yoshi** | Sometimes playable, later graphics and audio freeze; folder too | **Late runtime freeze** | O; freeze-point stack U |
| **DK Bananza** | New save sometimes enters interactive gameplay; loading existing save stalls; NUP/folder freezes | **Multiple modes: NUP blocked storage vs folder storage-idle global stop** | O |
| **Drag x Drive** | Gets through animated loading/partial game, freezes; sometimes closing via HOME crashes | **Late loading/runtime stop** | O; exact first playable action U |
| **OOT** | Main starts/ROM reads, no confirmed first visible frame in failing tests | **Early startup exit/abort** | O |
| **Kirby Air Riders** | Black screen/early close, no confirmed first visible frame | **Early startup failure** | O |
| Mario Tennis | Previous launch failure, no dependable gameplay | Early or unknown | O/U |
| Minecraft S2 | Software error, does not reach dependable gameplay | Early failure | O/U |
| Welcome Tour | Previous launch failure | Early failure | O/U |
| Mario Kart World | Actual installed host/override process | Real native host, NOT a guest | V/O |

**Five independent signatures:** (1) successful sustained guest; (2) direct native NUP SD IPC stuck; (3) idle guest/deadlock with watchdog still alive, frame/IPC/RPC flat; (4) early SDK abort/exit before first frame; (5) native PresentTexture returns with *black visible output*. All may be related at the architecture level, but NO log currently proves one single cause.

## 2. Correctly decoded META main.npdm for EVERY included title

**Verified byte offsets:** 0x0C MMU flags; 0x0E main-thread priority; 0x0F default core; 0x14 private system-resource / PersonalMmHeap reservation; 0x1C main-thread stack. **Important correction:** Earlier tentative interpretation called the 0x27/0x37 values “priority 39/55”. That was WRONG: those are **MMU flags**. Actual main priority is **44 (0x2C) for every title**; all specify default core **0**. Do NOT use erroneous thread-priority classification.

| Game / variants contained in supplied archive | MMU flags | Priority / core | Private sysresource | Main stack |
|---|---|---|---|---|
| MKW base + update 917504 | 0x37 | 44 / 0 | **64 MiB** | 1 MiB |
| Star Fox base | 0x37 | 44 / 0 | **16 MiB** | 1 MiB |
| Fast Fusion single | **0x27** | 44 / 0 | **0 MiB** | 1 MiB |
| Yoshi single | 0x37 | 44 / 0 | **20 MiB** | **64 KiB** |
| DK Bananza base + update 458752 | **0x27** | 44 / 0 | **0 MiB** | 1 MiB |
| Drag x Drive base + update 65536 | **0x27** | 44 / 0 | **0 MiB** | 1 MiB |
| OOT base | 0x37 | 44 / 0 | **48 MiB** | 1 MiB |
| Kirby base + update 65536 | 0x37 | 44 / 0 | **32 MiB** | 1 MiB |
| Mario Tennis base + update 327680 | 0x37 | 44 / 0 | 32 MiB | 1 MiB |
| Minecraft single | 0x37 | 44 / 0 | 16 MiB | **2 MiB** |
| Welcome Tour base + update 262144 | 0x37 | 44 / 0 | 16 MiB | 1 MiB |

**Comparison findings:** 
- SAME (V): priority 44/core0 for every title — cannot predict behavior. All have NSO0 modules and a META NPDM.
- DIFFERENT (V): Fast Fusion, DK, and Drag x Drive share **0x27, 0 MiB, priority44/core0, 1 MiB stack**, yet **Fast Fusion works** and DK/Drag freeze. Therefore zero private system-resource reservation and MMU mode by themselves are NOT sufficient causes of instability. MKW's 64 MiB figure describes a kernel memory-management metadata pool, not the game's total free RAM.
- DIFFERENT (V): Yoshi requests **64 KiB main stack**, much smaller than other comparison games. The actual launcher allocation and secondary threads/stack guard require independent verification; this *does not prove a stack overflow*.
- UNKNOWN: the signed ACID kernel capability bitfields, full ACI0 service permission set, GPU allocations, total live thread count, actual memory heap high-water, native driver fences, exact firmware-specific allocation behavior; these were not measured.
- Historical Pokopia was host ID 0400a3a01ee4c000 and reported system-resources 0, unlike MKW 64 MiB. This is **NOT evidence about current MKW guest freezes**; Pokopia experimental support was rolled back.

## 3. NSO module/size comparison — every included game

All numbers below are sums of **NSO declared uncompressed section sizes**, truncated to MiB, **not** physical RAM totals, ROM file sizes, GPU usage or max resident heap. Count includes main, rtld, sdk and any subsdk.

| Game/variant | NSOs | Text MiB | RO MiB | Data MiB | BSS MiB | subsdk count |
|---|---:|---:|---:|---:|---:|---:|
| MKW base | 4 | 50 | 15 | 3 | 7 | 1 |
| MKW update 917504 | **5** | 54 | 15 | 3 | 7 | 2 |
| Star Fox base | **7** | **190** | 44 | 39 | **32** | 4 |
| Fast Fusion | 5 | 15 | 24 | 53 | 2 | 2 |
| Yoshi | 6 | **120** | 33 | **81** | 15 | 3 |
| DK Bananza base | 3 | 33 | 11 | 3 | 3 | 0 |
| DK Bananza update 458752 | 3 | 35 | 12 | 3 | 5 | 0 |
| Drag x Drive base/update 65536 | 3 | 34 | 11 | 2 | 2 | 0 |
| OOT base | 4 | 69 | 18 | 5 | 3 | 1 |
| Kirby base/update 65536 | 5 | 99 | 67 | 63 | 4 | 2 |
| Mario Tennis base/update 327680 | 3 | 8 | 6 | 13 | 2 | 0 |
| Minecraft | 4 | **301** | **88** | 13 | 11 | 1 |
| Welcome Tour base/update 262144 | 8 | 62 | 48 | 58 | 3 | **5** |

- SAME/DIFFERENT: Star Fox has **seven NSOs, a larger total text section than DK, Drag or Yoshi, and substantial BSS**, yet remains stable in its known-good build. DK and Drag have only three NSOs and still freeze. Thus number of modules and size are NOT sufficient causes.
- Yoshi's large static data section is a candidate memory-pressure differentiator, but Star Fox's large text/BSS and Fast Fusion's large data remain counterexamples to a simplistic total-size threshold. Actual live allocation and fragmentation remain U.
- NSO header flags: most supplied module headers 0x3f; **OOT main 0xff**, **Mario Tennis main 0x1b**; relevant to compression/relocation verification **ONLY if** actual first-fault context points there. Executable NSO ZBIC is separate from compressed NUP Zstandard payloads.
- MKW update changes actual host modules/SDK build ID but retains NPDM 64 MiB. The exact **installed** MKW version controlling real services must be recorded; host binary variant is not necessarily the archived base.

## 4. SDK/module identity, service capabilities, and comparisons

Every game contains a distinct sdk NSO. Partial SDK NSO build IDs from binary headers:
- MKW base **90ebba182862**, update **7a68b13d8818**
- Star Fox **41d4de4a3b78** (working control)
- Fast Fusion **bbaa095d525b** (working control)
- Yoshi **820cd7d096aa**
- DK Bananza base **b7fff614f2bc**, update **18265daea210**
- Drag x Drive base/update **f70a530f2fda**
- OOT **1643a838814f**
- Kirby base/update **32a9ac1480a**
- Mario Tennis **32a9ac1480a**
- Minecraft **dfb937c3d3e7**
- Welcome Tour base **cbf9c083bcbd**, update **d743946430d5**.

**Do not treat matching build IDs as guaranteed identical game behavior:** Kirby and Mario Tennis share an SDK build ID yet differ dramatically in main binary size and game logic. Drag x Drive matches its own SDK across provided update variant. DK update uses a *different SDK binary* from its base; symbolizing one against the other produces wrong offsets.

Source NPDM ACI0/ACID bytes contain service-name text for, among others, acc:u0, aud:u, auddmg:u, audsmx:u, bsd:u, ldr:ro, nifm:u, time:u. DK/MKW's embedded service text differs from Drag/Fast Fusion/Yoshi/Kirby in visible chat:u, stpl:u, mnpp:app entries. **This is only a byte-level string observation, not validated complete ACL parsing or proof that a service was actually called.** Fast Fusion vs Drag having similarly visible service names and opposite stability falsifies “one service string causes freeze”.

**Host identity:** In this architecture every guest is in **MKW's native process and PID**, and real Horizon appletOE/VI/NV/audio/RO/FSP may observe MKW's rights rather than a genuine guest process. Host and guest dynamic requirements can disagree. Some errors (e.g. appletOE CMIF 0x480) also occur in **working** Star Fox, so those particular codes cannot be called the failure cause without corresponding behavior and callsite.

Sources: https://switchbrew.org/wiki/NPDM and https://switchbrew.org/wiki/SVC and https://switchbrew.org/wiki/17.0.0. Switchbrew's firmware semantics may require verification on the user's Switch 2.

## 5. Cross-game hardware timeline and exact failure signatures

### 5.1 Star Fox — WORKING positive control; experimental regression is historical
- **Stable MKW NUP** from uploaded BackupLoaderMKW-crash(20261123-050542).log: by elapsed **1,420,130 ms (~23m40)**, main entered, **73,510 native present returns**, **17,708 storage RPCs**, **7,385,754,289 ROM bytes counted**, **3,191,999/3,191,999 completed IPCs**. Storage idle at last sample, no guest or host abort; user confirmed actual interactive gameplay. This is a **positive behavioral control** and shows sustained ROM streaming and millions of real IPC transactions can function.
- Later **abandoned experimental branch**, NOT baseline: 19:42 L+A Star Fox NUP black-screened for >160 seconds; ~212MB read in first ~10 seconds; IPC still progressed. At 19:49 A-mode with restored stdio backend also remained visibly black for >110 seconds despite 377/377 native PresentTexture returns at 10s, 977/977 at 20s, 5,777/5,777 at 100s, **6,264/6,264 at 110s**, and 205,056 IPCs done. No long storage wait or abort. Source logged backend stdio_proven_default; the earlier claim that “native NUP reader causes black screen” is not proven and rollback did not fix it. User requested wholesale code reset; these experimental regressions should NOT pollute baseline conclusions.
- Useful distinction: **native PresentTexture returned** means only that the wrapper called the native function and it returned. Native GPU work could still be async, the VI layer may be invisible, or a different layer may be displayed. No actual scanout instrumentation exists.
- H: applet/display release and VI layer ownership, GPU queue/event, process-host lifetime, native service state; R: compare exact display-layer and NVN window ownership on proven working version vs failed version, keeping source mode/host update constant.

### 5.2 Fast Fusion — second WORKING positive control
- User consistently reports visible playable, non-freezing Fast Fusion through MKW. The supplied Fast Fusion ExeFS has **0 MiB NPDM private system-resource reservation, MMU 0x27, main priority44/core0, 1MiB main stack, 5 NSOs/2 subsdks**. This is the **direct control for DK/Drag 0MiB/0x27** and prevents attributing those games' freezes to private system-resource mode alone.
- U: the exact NUP-vs-folder format, applied game update, frame/IPC/audio timeline, native NVN queue and display state in a comparable log were not provided at the same granularity as Star Fox. NO made-up frame counts or throughput for Fast Fusion. Its working status is user observation.
- R: if a freeze-point recorder is implemented, collecting one low-rate, same-commit 2–5-minute Fast Fusion control trace would help distinguish background activity from real stalls, but no repeated generic testing without a concrete reason.

### 5.3 Yoshi — FOLDER runtime freeze, not attributable solely to NUP format
- Uploaded MKW Yoshi **folder** paired reports: BackupLoaderMKW-crash(20261123-194406).log, -gameplay(20261123-194406).log.
- At **10s**: main=1, frames=**5**, RPC=**4,142**, rom_bytes=**446,791,930**, NVN bootstrap=2, getproc=2180, present=5/5, IPC=4,234/4,234, active storage age=0ms.
- At **20s**: main=1, frames=**413**, RPC=**4,715**, rom_bytes=**496,861,434**, NVN bootstrap=2, getproc=2180, present=413/413, IPC=14,910/14,910, active storage=0. Both video and data advanced until this checkpoint, with no recorded host/guest abort or long SD call. **The file ends BEFORE the user's reported freeze.** No one should claim this particular sample proves the precise initiating stall.
- Other historical user tests: gameplay/save selection possible; sometimes freezes near Joy-Con vibration/HID command 214; earlier “NVN queue flush after 49s” was considered, but not tied conclusively to current baseline. Sound and picture both freezing is a real first-hand report, though absence of independent audio queue counters means cause/order U.
- **Binary differences:** 64KiB NPDM main-thread stack versus 1MiB most games, 6 NSOs, unusually large declared 81MiB static data and 15MiB BSS, MMU 0x37. Real kernel heap/sync burden remains unmeasured.
- H: event/sync starvation, NVN queue flush, native audio renderer buffer wait, HID vibration service/event, hidden VM fault, stack/guard or thread priority/affinity. R: capture first blocked native call + thread and independent audio completion around the freeze; compare to working Star Fox and Fast Fusion.

### 5.4 DK Bananza — multiple **distinct** failure modes, MUST NOT be conflated
- **MKW NUP 04:35** BackupLoaderMKW-crash(20261123-043533).log: at **231.8–241.8s**, only **3 frames**, RPC=2104, ROM bytes=302,139,524, storage slot=1, **storage age ~220,582–230,582ms**. This demonstrates a real *blocked native NUP storage service call*. Other historical NUP run partial transfer **589,824 / 851,968 bytes** at raw file extent index **105381**; a separate test saw codec0/raw file index **81812**. Do not claim these are the same blocked offset or same version unless logs prove it. Fix needs exact bounded FS operations and a safe way to recover from genuinely hung native IPC.
- **MKW folder 04:38** BackupLoaderMKW-crash(20261123-043823).log: frames **1,899**, RPC **2,120**, ROM bytes **332,472,928**, IPC **41,200/41,200**, storage **idle**, unchanged from 90s to 100s while watchdog continues. **This is not NUP I/O or a pending SD read.**
- **MKW folder 05:12** BackupLoaderMKW-crash(20261123-051251).log: at 130s frames **5,175**, RPC **4,851**, ROM bytes **647,873,983**, IPC **99,623/99,623**, active storage=0, and the *same four counters* at 140/150/160/170s. No reported fault/abort. **Guest application progress completely stops while independent host watchdog is alive**. This is strong evidence for a wait/deadlock/blocked guest thread, but initiating API U. User observed gameplay beforehand, especially new-save/mining; previous-save load sometimes fails earlier.
- **MKW NUP 05:15** BackupLoaderMKW-crash(20261123-051534).log: ~2,279 frames by 50 seconds, continued reading; behavior differs from 04:35 stuck NUP read and the idle folder freezes. Need distinguish version/backend/load-save conditions.
- **Pokopia** folder 18:50 is OUTSIDE requested MKW-host matrix. It entered DK base SDK, reached an actual SDK_BREAK at sdk text offset ~0x9e54c (return near 0x9e55c), about 650ms. RPC op32 was break, NOT blocked ROM; prior host context was different. A matching base-vs-update SDK binary check proves this trace used base SDK. Do not confuse this early Pokopia break with MKW gameplay freeze.
- **Binary differences:** DK base/update 3 NSOs, 0MiB/0x27, main-thread priority 44, 1MiB stack. DK update 458752 SDK build ID differs from base. R: instrument exact wait cause in idle-folder case; separately protect native NUP read worker/short-count handling.

### 5.5 Drag x Drive — rendering then stalled
- User's earlier MKW history includes animated loading progress and some gameplay/interaction claims followed by freeze or HOME-close app crash. One earlier trace described **160/160** presentation returns when the game stopped. Exact last live kernel/IPC/audio/NVN wait was not captured. Do not assert a complete interactive session or engine identity from just 160 frames.
- Shares DK's **0MiB/0x27, 3 NSOs, 1MiB stack** and priority44/core0; however **Fast Fusion also shares the memory mode and is working**. H: real differences in GPU command streams, output layer/VI, gameplay service requirements, synchronization, rendering work types or input/audio are more useful than these static fields.
- R: instrument whether first wait is NVN flush/finish/acquire, audren update/voice buffer, HID event, guest IPC or another kernel wait; compare last successful transition and first nonreturned call with DK.

### 5.6 OOT — early boot failure, not known late freeze
- Prior MKW OOT NUP reports include BackupLoaderMKW-crash(20261123-164105).log and (...175418).log. Game enters main and reads substantial ROM (~118,768,985 bytes by ~3.65s in a cited run), but no confirmed first native visible frame before exit/close. NPDM requires 48MiB versus real MKW 64MiB, so MKW's system-resource minimum is met. Native ZBIC/NSO main flag 0xff is unusual, but decoder support and verification already existed; can't claim ZBIC failure without first real fault.
- RO/guest process identity and early applet/SDK services are candidates. **appletOE cmd1 CMIF 0x480 appears in working Star Fox too; it is not itself proof of a fatal error.** Pokopia 0-vs-48MiB host-policy refusal was a distinct older experiment now rolled back, not an MKW diagnosis.
- R: first failed service/kernel result + caller module offsets; actual SDK break, RO registration with real process handle/permissions, first NVN/VI initialization outcome, route/close semantics of virtual guest FSP; no universal stubbed success.

### 5.7 Kirby — early startup or software close, no reliable frame
- Previous MKW Kirby Air Riders NUP/folder black screen and early close. Earlier experimental attempts logged ldr:ro command **4** result **0x1759** for a build (also seen with Star Fox in that particular experimental build), causing SDK break before NVN. This was not conclusively confirmed as same root in baseline and must not be transplanted blindly. Main 5 NSOs, 99MiB text+67MiB RO+63MiB data, 32MiB NPDM private reserve, 1MiB stack; resource alone doesn't explain (Star Fox more text and works).
- R: preserve genuine module registration return codes and actual caller handle, emulate none of the required process services as successful unless a verified equivalent exists. Verify dynamic module vs statically mapped NSO distinction and guest PID legality.

### 5.8 Additional complete collection
- **Mario Tennis:** 3 NSOs, 32MiB reserved, small ~8MiB total text, main NSO header flags **0x1b**. Earlier failed tests despite small code → disproves “smaller executable automatically works.”
- **Minecraft:** 4 NSOs, very large ~301MiB declared text, 88MiB RO, **2MiB** main stack, 16MiB reserve; user encountered system “software needed to close”. Could have a distinct high-memory or engine dynamic-module issue.
- **Welcome Tour:** **8 NSOs** / **5 subsdks**, 16MiB reserve, large static data/RO; earlier launch failures but no precise last-call trace in this report. Module count alone doesn't explain: Star Fox has 7 and works.
- **MKW host:** installed game version is real host. Supplied archive has MKW base 4 NSOs and update 917504 with 5, differing SDK ID; both META declare 64MiB reserve, priority44, core0, stack1MiB. The launcher takes MKW PID/kernel capability/host applet, and maps guest NSOs/ROM virtual FS in same process; guest's NPDM requests are NOT automatically inherited. This is a cross-title systemic architecture constraint.

## 6. Direct comparison matrix — SAME / DIFFERENT / UNKNOWN (MKW guest runs only)

| Factor | Star Fox WORKING | Fast Fusion WORKING | Yoshi FREEZES | DK FREEZES | Drag FREEZES | OOT STARTUP | Kirby STARTUP | Significance |
|---|---|---|---|---|---|---|---|---|
| Host PID/title | **MKW** | **MKW** | **MKW** | **MKW** | **MKW** | **MKW** | **MKW** | SAME; host identity alone doesn't choose outcome |
| Separate guest kernel process | No | No | No | No | No | No | No | SAME; architecture limitation shared |
| Visible in-game frames | Yes | Yes | Yes, intermittent | Yes, intermittent | Partial | No confirmed | No confirmed | DIFFERENT stage of failure |
| Long-lived stable gameplay | Yes | Yes | No | No | No | No | No | Observed user outcome |
| Source NUP/folder matters | NUP works | Format U | **Folder freezes** | **Both** with distinct failure modes | Source effect U | NUP fails | NUP fails | NUP-only theory disproved by folder freezes |
| MMU byte 0x0C | 0x37 | **0x27** | 0x37 | **0x27** | **0x27** | 0x37 | 0x37 | **No clean stability partition** |
| NPDM private sysresource | 16MiB | **0** | 20MiB | **0** | **0** | 48MiB | 32MiB | Host MKW 64MiB; Fast Fusion counterexample |
| NPDM requested priority | 44 | 44 | 44 | 44 | 44 | 44 | 44 | SAME across all; exclude as static discriminator |
| NPDM default core | 0 | 0 | 0 | 0 | 0 | 0 | 0 | SAME |
| NPDM main stack | 1MiB | 1MiB | **64KiB** | 1MiB | 1MiB | 1MiB | 1MiB | Yoshi-specific candidate, NOT shared DK/Drag cause |
| NSO modules | 7 | 5 | 6 | 3 | 3 | 4 | 5 | No stability partition |
| Native PresentTexture return | >70k in stable log | U count | >400 before sampled stop | >5k then frozen | Earlier ~160 | Not confirmed | Not confirmed | App-side count != display scanout |
| ROM service idle **at stop** | No stop | No stop | Unknown | **Yes** some folder tests | U | Not applicable | Not applicable | DK late freeze not SD read |
| Native NUP SD blocked | Not in stable run | U | U | **Yes** separate NUP test | U | No in cited read sample | U | Different failure mode |
| Audio callback completions | U (audible user gameplay) | U | **U at freeze**, user reports silence | U | U | U | U | High-value missing observable |
| Real NVN fence/flush wait stack | U | U | U | U | U | U | U | High-value missing observable |
| Host watchdog continuing while guest frozen | N/A | N/A | Trace stops before event | **YES** in folder logs | U | N/A | N/A | Distinguish guest deadlock from killed host |
| Last guest IPC callsite at freeze | N/A | N/A | U | U (idle IPC) | U | Limited early tail | Limited early tail | Missing block reason |
| Real game engine identified from ExeFS | U | U | U | U | U | U | U | **Do NOT invent engine names** |
| Full native update/patch match verified | U | Single archive | Single archive | Base and update differ | Base/update in archive | Base archive | Base/update in archive | Package match matters |
| Actual GPU memory usage | U | U | U | U | U | U | U | Needed for GPU/VM hypothesis |
| Actual per-thread core/priority/wait | U | U | U | U | U | U | U | Needed to test scheduler hypothesis |

**Negative conclusions proven by counterexamples:** 
1. “0 MiB private system-resource games freeze” → contradicted by **Fast Fusion working**.
2. “Huge binaries can't run” → contradicted by **Star Fox's 190MiB NSO text, 7 modules, sustained gameplay**.
3. “Folder fixes every freeze” → contradicted by **Yoshi and DK folder late freezes**.
4. “All freezes are blocked native SD I/O” → contradicted by **DK folder storage-idle repeated samples**.
5. “Completed present calls prove visible frames” → contradicted by **abandoned Star Fox A-mode regression**, native PresentTexture returns with visible black output.
6. “Missing system-resource reservation is just no RAM” → false; it is private KProcess metadata reservation, not total process memory.
7. “All games have different main-thread priority” → false; actual priority is **44 in all provided META**.

## 7. Prioritized *unproven* root-cause register and fix candidates

**Rank is for investigation yield/reproducibility; not claimed likelihood.** Each hypothesis has evidence FOR / AGAINST or missing information / a *real* corrective approach. For the existing baseline, any change requires targeted review and protected regression comparison.

### P1 H1: NVN queue, GPU fence, VI event or blocked graphics synchronization
- **For:** Yoshi, DK and Drag stop rendering after some visible gameplay; DK folder idle freeze keeps watchdog alive while app-side frames, IPC, ROM RPC all stop simultaneously. A single blocked high-level game/render thread could stall producer threads or a game-level mutex.
- **Against/unknown:** Native GPU queue-submit/flush/finish/acquire *return* phases and fence completion were not recorded at freeze. Star Fox's stable video does not rule out a workload-specific GPU condition. User audio silence may be downstream of game tick starvation.
- **Fix/test:** Keep original NVN function ABI, intercept only with exact typed wrappers that atomically stamp call begin/end and driver/native return; track nvN QueueSubmitCommands/Flush/Finish, WindowAcquireTexture, QueueAcquireTexture, QueuePresentTexture; include VI layer/vsync and NVDRV ioctl result. On 3–5-second liveness gap, watchdog writes one bounded freeze snapshot of last active call and queue/window. If GPU fence stuck, correct queue ownership, event/fence semantics, VI handle, or GPU buffer cache coherency. **Do NOT fake GPU fence completion or skip flush.**

### P1 H2: Native audio service/renderer voice/output wait
- **For:** Yoshi's *observed* sound and video halt at about the same time; some games block their game tick on audio event/renderer buffer state. Source services include aud:u / auddmg:u / audsmx:u; execution patterns unknown.
- **Against/unknown:** No independent submitted/completed audio-buffer counters, audren service timestamps or live wait stacks. Silence can result from *another* thread stalling first.
- **Fix/test:** RAM-only ring for audren/audout service begin/end + voice/render-buffer sequence/timestamps, kernel handle/event wait, audout buffer release. Compare **order** of first stalled audio event vs first stalled NVN callback. Fix real audio buffer-lifecycle or event ownership, priority/affinity only if observed. **No fake audio success or discarding audio requests.**

### P1 H3: HID/vibration/haptic sync during interaction
- **For:** Previous Yoshi freeze near Joy-Con vibration / HID214, DK stops near mining action, Drag may use advanced motion/controller facilities. Gameplay transition triggers richer calls than menus.
- **Against/unknown:** No repeatable blocked command 214 at precise late-freeze sample. Star Fox/Fast Fusion can use HID too. A correlation with input is not causation.
- **Fix/test:** Track real HID and vibration command + caller, begin/end and returned CMIF. Reproduce with input type changes/rumble disabled only as **explicit diagnostic A/B**; if caller blocks, correct native service object/event/permissions, not globally discard haptics or forge OK replies.

### P1 H4: Guest thread deadlock, priority starvation, waiting on shared synchronization primitive
- **For:** DK folder shows entire guest work visibly idle for 40+ seconds while watchdog still runs. This is classic signature of blocked guest work/deadlock but not a proof of *which* lock. Distinct SDK/engine thread topology under MKW process may create incompatible core or service assumptions.
- **Against/unknown:** NPDM priority44/core0 are SAME across all games. No complete guest thread list or wait-object dump available. One rendering thread can block without all threads deadlocking, even when ROM/IPC counters flat.
- **Fix/test:** Low-frequency thread-state samples, per-thread last successful progress and active wait call, permitted core mask/actual affinity, guest thread creation and stack guard, RTOS/kernel wait result. If deadlock, repair lock ordering or async completion, split storage worker from any synchronous guest callback, preserve actual priority permissions.

### P2 H5: Shared ROM RPC ring/re-entrancy/worker lock inversion
- **For:** All games use host-serviced ROMFS virtual file range RPC. Native NUP blocking can starve the whole storage worker. A synchronous callback into the same worker or ownership bug could stop guest reads.
- **Against:** DK folder idle-freeze shows *no active storage request*. That rules out “currently blocked sd read” at that moment, NOT a guest thread waiting elsewhere for an unissued or unpublished RPC.
- **Fix/test:** Stamp ownership/state transition (claim/publish/serve/complete/release) for RPC slots; record uncompleted vs unpublished queue entries and per-worker tick. Inspect lock order between guest file mutex and synchronous storage API. Fix only verified race, fairness or re-entrancy; no forced status successes.

### P2 H6: Title process identity, RO registration, FSP child/interface and applet service incompatibility
- **For:** Guest has MKW's PID/service permissions; real user account, save/caches and SDK may require their own title. Early Kirby/Star Fox experimental RO 0x1759 and DK Pokopia SDK Break highlight this class.
- **Against:** Star Fox + Fast Fusion work in same architecture; appletOE CMIF 0x480 occurs in a working Star Fox run. An error code existing in a tail does not prove it is the cause.
- **Fix/test:** Identify *first truly fatal* native CMIF/kernel result and SDK callsite, actual owner and object kind, actual RO registration process handle; use guest-aware real API adapter or a separate kernel process if essential. **Do not implement generic stubs/null/skips for required services.** Early OOT/Kirby should be investigated separately from DK/Yoshi late freeze.

### P2 H7: Memory plan, heap/alias fragmentation, VM access/cache coherency, stack safety
- **For:** Yoshi 64KiB declared main stack and large 81MiB static data; DK/Drag optimized 0-resource MMU; native NVN and audio allocations evolve after entering gameplay. Also possible GPU command/data coherency problem.
- **Against:** Fast Fusion shares optimized MMU mode and works; Star Fox is much larger code; no measured VM error at DK idle freeze.
- **Fix/test:** Measure real heap, alias free region, physical committed, BSS/stack guard and guest thread high-water, QueryMemory permission states, NVN GPU heap allocations/caches at freeze, actual native invalid-memory returns. Restore correct allocation/permissions and memory mode based on kernel results, not fabricated metadata. Separately verify Yoshi's **actual allocated** stack (the NPDM number alone is not proof of kernel stack size).

### P2 H8: Display layer ownership and host applet lifecycle
- **For:** Abandoned experimental Star Fox displayed solid black despite successful PresentTexture calls, while code still executed and IPC completed. Such a failure cannot be explained by slow ROM reads alone.
- **Against:** Reverted baseline once visibly played Star Fox; cannot assume same display failure causes Yoshi/DK late stop. Other causes include drawing black, driver/GPU command fault.
- **Fix/test:** Compare exact host appletExit/VI layer creation and visibility, NVN acquired textures, queue/window pointer lifetime, native results and display release between baseline working and an isolated reproducer. Repair real VI surface ownership; avoid force-presenting a fake buffer.

### P2 H9: Cache/save content, guest metadata/update mismatch or service runtime configuration
- **For:** DK differs between new save and existing save and NUP update variant can differ from extracted folder. ExeFS collection has distinct DK base/update SDKs and MKW host updates. Stale per-profile cache, wrong update overlay, savedata compatibility may trigger an otherwise valid game to wait.
- **Against:** DK freezes after actual gameplay and storage-idle, so missing NUP content is not always the immediate cause. Fast Fusion stable on same SD/host.
- **Fix/test:** Validate title-ID and active base/update/sd savedata mapping in log, cache key versioning, exact file-level SHA and update priority. Replay same known-compatible save and new save without modifying live save. Implement missing real FS methods rather than pretend file exists or bypass errors.

### P3 H10: Blocking native NUP storage/read backend
- **For:** DK NUP actual storage slot >230 seconds old; earlier native FS read partial bytes. This is **proven in that individual run** even though cause inside kernel FS is U.
- **Against:** DK folder late freezes with storage idle; Yoshi folder freezes; Star Fox NUP streams >7GB successfully in good session.
- **Fix/test:** Bound native reads, exact results, robust returned-error recovery, bounded staged buffers, isolate SD request worker so read stalls do not block watchdog; genuine cancellation/reopen only if kernel API allows. Distinguish short read vs blocking call. Do not retry forever, invent missing bytes, force extraction, or prefetch entire huge NUP.

### P3 H11: NUP indexing, metadata, compressed random reads and cache miss overhead
- **For:** User observes NUP loading slower than folder; NUP has container TOC, package and update overlays, Zstd frame compression, additional random seek and SHA/decode; folder has direct SD files.
- **Against:** Initial Star Fox NUP read ~212MB in 10s in failed experimental build; high throughput doesn't imply visible game, and NUP speed cannot fix folder idle deadlock.
- **Fix/test:** Benchmark wall-clock TOC parse, SHA, index creation, exact fsFileRead, Zstd read/restart, decompressed cache hit/miss, speculative raw read-ahead, first *visible* frame. Persistent validated TOC/ROM index keyed on archive size/mtime/header hash, safe extent-page caching, bounded prefetch and package update overlay lookup; preserve exact byte offsets, zstd SHA check, and no extractions. Optimize only shown bottleneck and test Star Fox + Fast Fusion controls before rollout.

### P3 H12: Logging overhead, SD driver, external host suspension or runtime HEN state
- **For:** Heavy diagnostic flushes previously contended with storage, SD read blocked in some DK NUP runs, stopping video/audio could be system-wide. HOME close sometimes crashes.
- **Against:** DK folder watchdog continued writing periodic state while game counters were frozen; entire host was not dead there. Star Fox uses same SD successfully.
- **Fix/test:** Collect counters in RAM (not per-frame SD), watchdog writes only one low-rate failure snapshot, track log flush time, host storage last-poll and raw SD IO latency, inspect libounce/HEN termination separately. Do not induce freeze by adding exhaustive logging.

### P3 H13: NSO load/relocation ABI, SDK assumptions or dynamic modules
- **For:** OOT 0xff NSO flags, Kirby multiple subsdks and previous RO issue, DLL-like dynamic modules can expect guest process identity.
- **Against:** OOT codec handled/verified earlier; Star Fox has 7 NSOs and works; small DK sometimes loads gameplay. No current map fault proves a loader ABI bug.
- **Fix/test:** Verify every NSO segment hash, relocation+RELR+branch entries, TLS, static initializers and genuine dynamic RO registration against **actual variant**. Fix isolated genuine relocation/ABI bug, not title-specific null stubs.

### P3 H14: Guest clock/tick, AV pacing, event sequence, asynchronous state or time-dependent scene transition
- **For:** Some runs last dozens of seconds then stop around a menu, save, action or scene; audio and graphics may share a single game-loop barrier.
- **Against:** Need precise freeze time, last game action and per-thread/event waits. Delayed freeze alone could be anything.
- **Fix/test:** Record monotonic host timestamp and guest thread tick, last input category, game read phase, audio/gfx completion and last event wait; isolate reproducible action rather than adding arbitrary timeout skipping.

## 8. Design for the **one** high-value executable investigation (NOT YET IMPLEMENTED)

Implement a universal, low-overhead **freeze-point liveness recorder**, applicable to all guests, on an isolated experimental commit AFTER verifying the rollback:
1. Atomic counters/time of **enter and return** for native NVN present, submit, flush, finish, texture acquire, VI, NVDRV; preserve exact ABIs and native return codes. If no native callback hook for a function, mark it **unobserved**, not zero usage.
2. Atomic last IPC service/command/object/caller and completion, main/worker scheduler progress, storage RPC slot/op/time, native file handle and exact (file, offset, length) for blocking I/O. Reuse existing in-memory IPC ring; never add synchronous RPC solely for diagnostics.
3. Independent **audio renderer/output** submit/completion/event timestamp and HID/vibration IPC native result. Label buffer/fence sequence, not personal user data.
4. Watchdog checks every ~2s: when *actual guest progress* stops for >=3–5s and host/watchdog continues, freeze one **bounded snapshot** with the most recent outstanding native operations and per-thread wait if available. Do **not** kill the game or fabricate completion. Avoid heavy SD writes while game is healthy.
5. Reproduce on Yoshi folder and DK folder with same MKW host; compare Star Fox and Fast Fusion under same code commit and A mode. Distinguish **native wait entered but never returned** vs **no calls issued because game loop blocked upstream** vs **host/watchdog stopped entirely**.
6. Only then patch the identified real operation: correct NVN/fence/surface ownership, audio event lifecycle, HID command, lock order, memory permission or storage worker. Add acceptance tests verifying *real user-visible play and audio* beyond previous freeze point. No title-specific hardcoded “ignore error” behavior.

**Potential test outcomes:** (a) NVN flush entered and not returned → GPU/fence/queue handle; (b) audio event wait first → audio backpressure; (c) HID214 never returns → controller/haptic path; (d) all native work returns yet game-loop stalls → guest mutex/fiber/time barrier; (e) storage op active for many seconds → SD service thread; (f) storage idle and all app counters flat while watchdog alive → guest-side deadlock/unobserved wait; (g) watchdog stops too → host/system termination/suspension. These outcomes discriminate hypotheses instead of generating indefinite generic logs.

## 9. Engineering guardrails, sequence and acceptance

- **P0:** Preserve actual playable source branch baseline-playable-2026-11-22-848a47d. experimental-nup-support now uses identical baseline executable and tests, with only tracking files added. Do not “optimize” the working games without targeted rollback-safe verification.
- **P1:** A single GPU/audio/HID/thread **freeze-point collector** from section 8. Do not change scheduling, memory policy or service success values until the collector identifies a concrete native stalled call/thread or live error.
- **P2:** Address specific late-freeze mechanism, with Yoshi folder and DK folder regressions first and Drag next. Validate Star Fox and Fast Fusion *visibly* play and that audio remains live; metric-only “frames” is inadequate.
- **P3:** Independently benchmark NUP performance, correctly handle actual native storage stalls and optimize validated cache/metadata bottlenecks without extracting NUP or compromising update overlays. Compare same title/variant in folder vs NUP, wall-clock seconds to first visible frame, not just aggregate ROM byte counter.
- **P4:** OOT and Kirby before-first-frame crashes require separate SDK/RO/FSP/graphics readiness investigation.
- No GitHub Actions, no PRs, local build.bat + user hardware. No changes to existing save data, no guest SDK success stubs, no wholesale bypass of required API. Keep all Findings/HANDOFF history append-only.

## 10. Remaining unknowns / limits of this COMPLETE inventory

1. **Not yet known:** exact blocking PC/thread/wait object for Yoshi/DK/Drag late freeze; actual independent audio callback progression, GPU fence statuses, driver error and actual display scanout timing.
2. **Not yet known:** true runtime engine names/versions and whether these games share game engines. Static 3-NSO counts are not evidence of the same engine.
3. **Not yet known:** full detailed ACID/ACI0 capability bitfield decoding and title-specific actual service access traces for each game, true host installed update version, and exact NUP update manifest used during each test.
4. **Not yet known:** exact Fast Fusion controlled log timeline or NUP backend; working status is based on user's real gameplay report, not fabricated counters.
5. **Not yet known:** 100% decompilation or comparison of arbitrary NSO code; header and SDK build-ID audit is complete for supplied archive, but full binary reverse-engineering/trace instrumentation is different work.
6. **Not yet known:** comparative physical memory, alias/heap, GPU heap high-water, async audio buffers, and user-input milestone at each freeze; proposed recorder exists only as an engineering specification.
7. **Not included:** Pokopia's NPDM binary. Collection has 11 titles listed above, not Pokopia; historical Pokopia GetInfo16=0 is hardware evidence, not direct ExeFS audit.

**Interpretation:** “Comprehensive investigation” here means ALL available ExeFS titles, metadata, module/SDK build variants, known hardware behaviors, contradictory controls, a diagnosis/fix matrix and the missing evidence explicitly inventoried. It does **not** falsely mean we have already proved a unique freeze root or fully reverse engineered every module. A successful fix requires hardware confirmation.

## 11. Source evidence index
- Archive: user-provided 7-part “exefs collections” with eleven title groups. Actual META fields and NSO headers programmatically enumerated in a Python one-pass audit; build IDs read from NSO header offset 0x40; sizes from segment descriptors, not guessed from ZIP compressed size.
- Stable MKW Star Fox: BackupLoaderMKW-crash(20261123-050542).log.
- DK NUP blocked native read: BackupLoaderMKW-crash(20261123-043533).log. DK folder idle freezes: (...043823).log and (...051251).log; DK NUP additional progress: (...051534).log.
- Yoshi MKW folder: BackupLoaderMKW-crash(20261123-194406).log and -gameplay(20261123-194406).log.
- Post-baseline Star Fox **reverted experiment**: (...194215).log L+A, (...194943).log A with stdio; no graphics visibility despite counters. Do not assume that is baseline behavior.
- OOT early runs: BackupLoaderMKW-crash(20261123-164105).log and (...175418).log. Kirby, Drag, earlier HID and RO findings documented with build-specific details in chronological append-only Findings.md and HANDOFF_2026-11-21.md.
- Preserved playable Git branch baseline-playable-2026-11-22-848a47d = 848a47dcf26fbdb8499aa2b552bfebd4fe2de170. Rollback commit b1f446f51919bfcb8088cc6cb2e9402aaddfed82.
- Verified definitions/background: https://switchbrew.org/wiki/NPDM ; https://switchbrew.org/wiki/SVC ; https://switchbrew.org/wiki/17.0.0 ; https://switchbrew.org/wiki/NV%20services .


### 2026-11-23 — Passive suspected-present stall recorder and NRO author Goose
- User asked how to determine which operation stops first; approved a narrowly scoped executable experiment, then requested NRO author **Goose**. Changes on experimental-nup-support only, protected baseline branch unmodified.
- main.c adds **host watchdog only** stall detection using existing atomic frame (returned native PresentTexture), IPC completion and ROM RPC completion counters: after >=30 observed presents followed by >=8 seconds with no further present in observed modes, emits one WARNING_SUSPECTED_PRESENT_STALL per non-progress episode, plus measured IPC change since last present, ROM RPC change since last present, active storage slot and age, limited IPC breadcrumb tail in crash report. A resumed frame resets the episode. Entirely passive, no new guest hooks, GPU/audio stubs, lock changes or scheduling. Mode L+A is **unobserved** so correctly does not trigger. A warning means missing present, NOT confirmed deadlock, not proof that GPU caused freeze or that visible video actually changed. It can miss never-first-frame failures and frames still presenting black; those require separate evidence. Source commit `693dec6970b3844e68734dc75b6bfa1dd7cc170e`.
- The packaging script scripts/embed-icon.ps1 (NRO build sets NO_NACP=1) now embeds 0x4000-byte NACP in standard ASET after the supplied JPEG. Name BackupLoaderMKW and author **Goose**, all 16 language slots (0x200 bytes name plus 0x100 bytes author in each 0x300 record), preserving existing icon. ASET NACP descriptor offset 0x18 size field at 0x20 (corrected according to https://switchbrew.org/wiki/NRO); romfs descriptor 0x28/0x30 remains empty. It validates final author bytes. Metadata commits `9593f666115e0f48e138142bc4e857e03538d32b` then layout correction `d4aed264267e16f30246148b0811cb0858ebf2b2`.
- Next hardware tests: pull experimental, build.bat local, verify HBMenu NRO details lists Author Goose, then MKW Yoshi folder with normal A until freeze (look for WARNING_SUSPECTED_PRESENT_STALL and WARNING_STALL_NATIVE_PRESENT in crash log). MKW DK folder follows if Yoshi uninformative. Compare known-good Star Fox/Fast Fusion before concluding regression-free. No need to re-extract NUP. If stall snapshot shows no IPC, no storage and stopped presentation, needs per-thread blocked wait/caller sampling; this patch is first discrimination only, **not a cure or identification of exact first blocked thread**.
- No ounce-devkit compiler or Switch 2 hardware available to assistant; source, ASET field layout and textual wiring verified remotely, behavioral compatibility needs user test. Do not assert successful compilation or visible gameplay yet.


### 2026-11-23 21:05:23 — MKW-host DK Bananza **FOLDER** freeze captured by passive detector
- Uploaded paired `BackupLoaderMKW-crash(20261123-210523).log` (1,896 lines) and `BackupLoaderMKW-gameplay(20261123-210523).log` (2,313 lines). Guest DK 040074a01bf12000 from **folder**; real MKW host 0400c3f00006e000; A/native-present-observed mode=1, 3 guest modules, 75,238 virtual indexed files, ~9.159GB virtual RomFS. This test of newly added passive stall detector DID fire. All timestamps below are elapsed guest execution, not RTC.
- At 135.5s frames 5175, RPC 4820, ROM bytes 644958551, IPC 100244/100244. At 137.5s frames 5295, RPC4828, bytes646465879, IPC102364/102364. At **139.505s**, frames **5361**, RPC **4851**, ROM bytes **647873983**, IPC **103663/103663**, storage idle. Every subsequent periodic host snapshot to **162.340s** shows the *exact same* counters and storage slot=0, guest_abort=host_abort=0 and fault_state=0, while watchdog continues. Thus over 22.8 seconds of observed app-side halt. This replicates and strengthens earlier MKW folder stall under DK; **NUP codec/FS active read is not required**, and source I/O itself did not remain active.
- `WARNING_SUSPECTED_PRESENT_STALL duration_ms=9728 frames=5361 ipc_end=103663 ipc_since_present=4 rom_rpc=4851 rom_rpc_since_present=0 active_storage_slot=0 storage_age_ms=0 nvn_mode=1 confirmed_deadlock=0`. Independent present=5361/5361 native return counter; wrapper does NOT identify GPU scanout or capture native NVN queue flush/finish because A mode leaves those unobserved (0/0 must never mean unused).
- Two different **IPC observability layers** need not have identical aggregate counts: `ipc_trace_begins/ends` exclude certain close messages, whereas `ipc_event_next` records broader events. The trace's final recorded HIPC event is `seq=207461 call=103731 phase=1 owner=appletOE handle=008f8161 hipc=125 command=0xffffffff caller=000000000abcafa0` and no matching phase2 appears before freeze. Nearby requests (including HID214, aud:u cmd4, appletOE cmd11) did return 0. **Candidate last unmatched synchronous IPC (H), not proven initiating event**; ring sampling, malformed/noncanonical HIPC and logger publication/ordering can confound. A-mode shows no storage pending and IPC further progress ceased.
- Prior to stall, ~18 IPC_EVENT lines report `cmif=001c266f` on handle tagged `appletOE`, `hipc=125`, `command=4294967295`, `hdr0=0010007d hdr1=00000003`, `kernel=00000000`, caller 0x000000000abcafa0. This is **not a validated CMIF error**: type125 is nonstandard for ordinary HIPC request, decode says cmd unknown, owner labels are traced handle tags, and helper may misparse non-CMIF data as a CMIF result. Do not alter appletOE behavior based on 0x001c266f without reconstructing actual HIPC format and validating that reply contains a CMIF result.
- Other frequent completed calls before freeze: `aud:u` command4, `hid` command214, `appletOE` command11, and HIPC types 119/217/245/977/330. These do NOT individually prove blocking audio/HID. No active native SD read. Visible graphics/audio freeze is user report, still no independent audio callback monitor.
- **Next highest-value change proposal, not yet implemented:** record exact per-thread SVC enter/return + monotonically sequenced slot and timestamp for `SendSyncRequest`, `SendSyncRequestWithUserBuffer`, `SendAsyncRequestWithUserBuffer`, and likely `WaitSynchronization`/Signal/Cancel under SDK patch if supported. For a specific last unmatched call, preserve *handle tag, raw HIPC header, thread ID, caller PC, count and start tick*, and native kernel return if any; do not use string heuristics to classify cmd=0xffffffff or automatically claim an `appletOE` response error. A small event-specific recorder on HIPC125 path is higher yield than another broad GPU wrapper. If actual SVC is blocked, inspect target kernel handle/service lifecycle; if it returned but log result missing, fix trace sequencing. Do not fake success replies, bypass waits, or modify guest source.
- Existing known-playable baseline remains protected; this test uses experimental commit after passive detector and NACP author Goose change. Crash detector has now furnished useful evidence, but **not a confirmed root cause**.


### 2026-11-23 21:10:29 — DK Bananza MKW NUP vs folder: urgent coordinated diagnostic batch
- New uploads `BackupLoaderMKW-nup(20261123-211029).log` (1524 lines) and `BackupLoaderMKW-crash(20261123-211029).log` (1057 lines). Host MKW title 0400c3f00006e000, DK NUP guest 040074a01bf12000, normal A mode. User observed **NUP reaches loading screen faster than folder** but freezes/crashes on loading rather than later during gameplay. This visible difference is real user evidence; **prelaunch time is a separate metric** and actually favors folder in these two runs: folder prior cumulative to guest worker ~1839ms, NUP ~4266ms; NUP indexing alone ~1293ms vs folder cached index phase ~295ms. Do NOT falsely claim NUP universally faster or assume this disproves the user's loading-screen experience. Runtime streaming, caching, path layout, update selection and different saved/loading state are H/U and must be measured separately.
- DK NUP: guest startup and native PresentTexture calls happened: 1865/1865 returned. At 114.111s frames=1865, rom RPC completed=3507, rom_bytes=476372999, IPC sync=35235/35235; identical for multiple later samples. Passive warning at +8 seconds stopped: `WARNING_SUSPECTED_PRESENT_STALL duration_ms=8000 frames=1865 ipc_since_present=15 rom_rpc_since_present=2 active_storage_slot=0`. This is **not an active NUP fsFileRead**. Crucial: **one pending guest RPC slot** remains `slot=0 state=2 op=1 (RPC_ROM_READ), a=0x5000, b=0, c=0x12c` throughout the stall, although `host->rpc_active_slot=0`. The host storage worker's `STORAGE_SERVICE_HEARTBEAT last_poll_ms` grows to **~67682ms** (at 114s): storage service loop stopped polling. Independent guest storage function `GUEST_FS_READ entries=3390 returned=3389`; ROM RPC trace last `ticket=3508 op=1 phase=1` unmatched. **This is the strongest concrete common causal chain yet: published ROM request not consumed because storage worker no longer polls; guest read remains outstanding; game frame/IPC activity stops.** It is still H that this specifically *initiates* video/audio stall rather than being secondary to a broader scheduler failure. No missing ROM data should be fabricated.
- The NUP IPC event ring last `seq=70593 call=35297 phase=1 owner=appletOE hipc=227 command unknown`; this could be a concurrent blocked native SVC or an incomplete trace, **not proof of appletOE error**. Previous MKW DK FOLDER freeze 21:05 likewise had a final unmatched IPC type125, raw nonstandard header and suspicious CMIF 0x001c266f; do not interpret as a normal applet service command/result without real decoding. In this NUP run, `aud:u cmd4` and `hid cmd214` completed before stall. The alternative hypotheses GPU wait, audio event and HID remain U at first-block point.
- Source review: `bridge.c::nx2_rpc` claims a state0 slot, writes request metadata then **publishes state=2 (release)** and waits for state=3 using bounded kernel sleeps; `host.c::nx2_host_poll` scans 16 slots for state2 and completes them. Storage worker in `main.c` polls continuously, sleeping 100000ns when idle. Therefore **state=2, no active storage, large last-poll age** signifies the worker failed to revisit polling (or observation inconsistency), distinct from a genuinely long active NUP SD read. Could be worker not scheduled, stuck outside polling, fatal thread exit, or trace inconsistency; per-phase capture required. We should not launch two simultaneous servicing workers or simply mark request completed—races and corrupted guest bytes are unacceptable.
- Executable instrumentation batch on `experimental-nup-support` (protected playable baseline never changed):
  1. `launch.h` adds per-request diagnostic publish timestamp, guest-owner TLS and RPC ticket and a bounded 128-slot per-thread native synchronous IPC flight table with raw HIPC headers, caller, handle and collision count. Commit `ec2ac707d72ed16852437b009219a202bc6225f9`.
  2. `bridge.c` stamps request before release-publishing state2, no change to original RPC order/response/ROM data. Commit `e2e349f4b5b0583717132b0b6bd66a184fb244f5`.
  3. `svc_hooks.c::filter_sync` records native sync entry/return with calling TLS, raw header0/header1 and native kernel result in a bounded hash; detects collisions rather than overwriting active calls from another TLS. No added synchronous diagnostic RPC or fake IPC. Commit `30e9220db556e0bd53cb4a293efda1b2f7ac466f`.
  4. Storage worker records phase=1 before host_poll, phase=2 after return, phase=3 while sleeping, and monotonic poll count. Commit `7cd19d3c5b1527f4bcf22ef6c1fa9752cee6c242`.
  5. Passive watchdog now writes `WARNING_WORKER_LIVENESS`, `WARNING_UNRESOLVED_RPC` with age/op/slot/TLS/ticket, `WARNING_NATIVE_SYNC_IN_FLIGHT` (age>=1s with raw HIPC headers, no decoding guesses), and `WARNING_SYNC_TRACE_COVERAGE` on present stalls. Additionally triggers on a published RPC >3s once per 10s **even before first frame**, without requiring native presentation. Commit `4de5affce4c74da9c5587d16eed91d5319e42082`. Original FS, NUP, NVN wrappers, audio/HID, priority, thread placement, applet and save behavior remain unchanged.
- **Limitations:** This is a substantial multi-subsystem *trace/fix-enabling* batch, not a proven functional cure. Does NOT guarantee kernel per-thread PC or wait-object capture; native SVC TLS hash may collide and reports coverage. Does not automatically reassign or complete stuck RPCs (unsafe without proving worker death/ownership). User asked every fix, but unsupported blanket workarounds would destroy signal and working Star Fox/Fast Fusion. No Switch 2 compiler/hardware available in assistant environment. Local `build.bat` and real hardware validation still required.
- **Test priority:** MKW DK NUP A (reproduces earlier loading-screen freeze) and MKW DK folder A (late freeze), with paired warning/error logs; compare `phase`, `poll_loops`, `last_poll_age_ms`, published slot age, per-thread native IPC flight; once stable working controls, Star Fox and Fast Fusion regression checks. If worker phase3 stale => sleeping/scheduling/wakeup issue; phase1 stale and active_slot0 => stuck inside polling wrapper outside explicit active op; phase2 stale => stalled after poll/before next loop; if active_slot nonzero => genuine storage I/O block. Distinguish a blocked guest RPC from an unrelated unfinished service request. No extraction justified.


- **Same-batch late addition:** `main.c` stores storage worker kernel handle at thread start and on stale-worker freeze uses `svcWaitSynchronization(handle, timeout=0)` **nonblocking** to determine if thread handle is signaled (terminated). Emits `WARNING_WORKER_TERMINATION_PROBE handle=... kernel_result=... terminated=0/1`. A timed-out probe indicates worker has **not signaled exit**, NOT that it is actively running. No worker restart, no thread kill, no ROM data alteration. Commit `2a790a0b3167d99e9c958c99dc8363ef722bc32d`. This directly distinguishes dead worker from not-exited-but-stalled. 

- Final defensive tracer correction: for RPC slots only CLAIMED (state=1, unpublished), watchdog now reports `unpublished=1` without reading stale request parameters from an earlier occupant. Only published state>=2 includes request op, age, TLS, ticket and offsets. Commit `056684f178d6ae4a715cc21b270d5e1c84cc6dd2`. This is diagnostics-only; no guest timing or filesystem semantics changed.


### 2026-11-23 21:21 — MKW DK folder, externally supplied 100% save: active ROM read stall (pre-new tracer build)
- User supplied `BackupLoaderMKW-gameplay(20261123-212100).log` (2504 lines) and `BackupLoaderMKW-crash(20261123-212101).log` (2109 lines), collected **before** pulling/building newest RPC/IPC worker-phase tracer batch. Real MKW host; DK Bananza `sdmc:/games/DK Bananza` folder; A/native-present-observed.
- User loaded a newly obtained externally supplied completed/100% save and observed freezing before the save fully loaded. DO NOT assume save corruption, mismatch, success of save load, or completion percentage is causal without save-specific errors. Different save changes gameplay load and resource demand.
- Guest entered main; reached **5900/5900 returned PresentTexture**, `rom_bytes=657803771`, `rpc_calls=6847` (worker completed=6846), `ipc_event_next=223985`, no reported host or guest abort or exception. Once frames froze, `rom_bytes`, RPC and IPC counters remained unchanged while watchdog still wrote entries up to **elapsed_seconds=192**. Last native NVN stages =0/0 because A mode does not instrument full stages; not evidence that flush/finish were unused.
- Unlike earlier MKW DK **folder** idle-storage freeze, this case has **one active published ROM read**: `RPC_PENDING slot=0 state=2 op=1 a=0x0cac b=0 c=0x02dc` (archive virtual file index=3244, offset=0, bytes=732; actual specific mapped file path not decoded in this log); `STORAGE_SERVICE_HEARTBEAT running=1 active_slot=1 active_op=1 completed=6846` and `active_ms/last_poll_ms` steadily grew to **45472ms**, while watchdog remained alive. This is *inside the currently-served ROM request*, versus previous DK NUP case with `active_slot=0` but published state2 while worker ceased polling. The critical commonality is **no storage-worker forward progress**, not necessarily the same underlying cause.
- `WARNING_SUSPECTED_PRESENT_STALL duration_ms=8000 frames=5900 storage_slot=1 ipc_since_present=0 rom_rpc_since_present=0` fired. This is strong evidence of an unresolved ROM service operation. Whether it caused game/video/audio to stop, or the worker itself encountered a separate native FS/kernel issue, remains H.
- Next diagnostic build just committed (latest experimental head after documentation) adds worker phase, poll loops, kernel termination handle probe, per-RPC publish timestamps/tickets/owner TLS, per-thread native IPC in-flight snapshots, and stale request detection. **It does not yet instrument internal `serve_rom_cached` steps or precisely identify which file path corresponded to virtual index 3244.** If next logs show worker phase=1 active_slot=1 and termination probe=false, a second-level read/cache/backend phase marker at `host.c::serve_rom_cached` is the next decisive addition. Do not auto-complete, retry concurrently or fake bytes.
- Historical comparison: (a) earlier DK folder idle active_slot0 stall; (b) DK NUP published ROM request active_slot0 with stale worker poll; (c) THIS DK folder worker ACTIVE op1 stale 45.5s. These are distinct states of the shared ROM RPC pipeline. Analyze differences without treating save provenance as proof.


### 2026-11-23 — DK Bananza stall discrimination / build repair
- Prior tests separated DK NUP (published ROM request pending, worker not actively servicing it) from DK folder 100% save test (active ROM request, index 3244, offset 0, 732 bytes, worker blocked while servicing). This remains an **observed distinction**, not proof of a universal stall cause or save-file defect.
- The previously unbuildable RPC publish-tick tracer has been repaired for guest compilation by using the AArch64 physical system counter directly. A follow-up passive tracer records active ROM request file index/path, offset, bytes and cache/backend phase at watchdog time. It is untested on Switch 2 hardware; do not call it a game fix until after actual runtime evidence.
- Next paired tests: DK Bananza NUP A and folder A; inspect `WARNING_WORKER_LIVENESS`, `WARNING_ROM_READ_STAGE`, `WARNING_UNRESOLVED_RPC`, `WARNING_NATIVE_SYNC_IN_FLIGHT`, `WARNING_WORKER_TERMINATION_PROBE`. Re-test a known-working Star Fox or Fast Fusion title to check for regressions.


### 2026-11-23 21:43 — DK Bananza folder vs NUP new host hangs
- Folder input `BackupLoaderMKW-crash(20261123-214339).log` / matching gameplay: freeze at native PresentTexture 3470 returned, RPC ticket 3761 unresolved, storage active_slot=1, phase=1; `WARNING_ROM_READ_STAGE phase=5 file=9479 offset=0 bytes=851968 path=Model/ZebraStyleHead.anim.bfres.zs`. Guest read entries=3623, returned=3622. Over 60 seconds of unserviced ROM read while watchdog remains alive and storage thread has not signaled termination.
- NUP input from 21:38:35 reaches 197 frames; worker liveness phase3 idle sleep / active_slot0, not the same confirmed active backend read. Host MKW closing from HOME hangs then reports software error in user's observations, warranting host-process shutdown investigation in addition to game-frame diagnostics.
- Fix attempt: folder backend ROM reads chunked to 64 KiB with checked short/error results. Added per-chunk progress and phase8 open / phase9 fsFileRead watchdog. No change to NUP path, save semantics or guest responses; hardware unverified. Re-test folder A and NUP A, then known-working controls.


### 2026-11-23 21:54 — DK Bananza failed 64-KiB folder-read experiment
- 21:54:15 folder gameplay/crash logs: game 2761 frames then freezes, 3371 completed / 3372 attempted RPCs. File stuck **at opening**, not reading: `WARNING_ROM_READ_STAGE phase=8 file=36238 offset=0 bytes=334 path=Tex/DonkeyKongBody_Nrm.bntx.zs`, progress completed=0 chunk_bytes=0; same pending ticket more than 40 seconds. Host close from HOME remains prolonged/forced error. User confirms slower folder loading from 64-KiB chunking without benefit.
- **Reverted** slower per-64-KiB folder read behavior in `e3b8a011c7fe12f244a1ea0be21a517ca27fac83`; preserved open/read-stage tracing. DK remains not stable; issue involves potentially blocking native FS operations, not exclusively large files. NUP unchanged.


### 2026-11-23 — All-games guest IPC logging policy (experimental)
- DK Bananza folder + NUP remain not stable; no game-specific fix in this pass. User directs launcher to favor native kernel/service results, except required isolated guest saves and host process safety. New primary-log `IPC_EVENT origin=` disambiguates all intercepted/native/virtual/rejected IPC request/result events, persisted by default (may generate large logs); SD flush cadence 2s, not per IPC. Optional `sparse-ipc-trace.flag` restores lighter persistence; in-memory ring/crash tail remain. No new automatic DLC support, no fake successful file reads, no removal of required fsp-srv isolation, and no proven host-freeze fix.
