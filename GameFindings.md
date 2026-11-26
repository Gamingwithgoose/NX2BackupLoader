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


### 2026-11-23 — DK/NUP experiment request fidelity diagnostics
- No changes to game-specific launch, DLC, save layouts, or native FSP boundaries. New complete IPC metadata logging and explicit `COMPAT_SYNTHETIC_SUCCESS_NO_KERNEL_CALL` / `COMPAT_SYNTHETIC_RO_INIT_SUCCESS` markers make synthetic SDK outcomes distinguishable from native IPC returns. DK hang still uncorrected and hard MKW close persists in previous hardware tests. Hardware validation pending.


### 2026-11-23 22:18–22:44 — Full-trace game matrix
| Game / source | New outcome | Last verified progress | Decisive difference |
| --- | --- | --- | --- |
| DK Bananza folder (22:18) | freezes | 191 native Presents returned; 2097/2097 host RPCs; 1986/1986 ROM reads; pending=0 | No active file read when presentation stalled; unlike earlier folder backend hangs |
| DK Bananza NUP (22:20) | freezes | 1922 native Presents returned; 2139/2139 host RPCs; 2021/2021 ROM reads; pending=0 | No active ROM RPC when presentation stalled; NOT same observed state as earlier folder hangs |
| Zelda OOT NUP (22:21) | ends early/no frames | 0 frames, ~8.9s, 609 guest fs reads entered, 607 returned, 2 pending | Insufficient duration to establish deadlock; no explicit SDK break in this capture |
| Zelda OOT folder (22:22) | ends early/no frames | 0 frames, ~7.0s, 463 fs reads entered, 461 returned, 2 pending | Insufficient duration to establish deadlock; no explicit SDK break in this capture |
| Starfox folder (22:24) | user reports successful launch/play/load | 3218 native Presents, 2776/2776 reads, no outstanding ROM RPC at ~64.5s | Proven preservation of working title under full tracing; RO synthetic init marker is not necessarily fatal |

Detailed facts: DK folder `crash(20261123-221823)`+`gameplay(20261123-221825)`; DK NUP `crash(20261123-222055)`+`nup(20261123-222058)`; OOT NUP `crash/nup(20261123-222159)`; OOT folder `crash/gameplay(20261123-222250)`; Starfox `crash(20261123-222439)`+`gameplay(20261123-222441)`. Full text trace loses events: Starfox ~207082 and DK NUP 31132 `IPC_EVENTS_DROPPED`; these are observer overruns, not guest IPC rejects. Starfox's `chat:u` 0x4e2 CMIF and `bsd:s`/`bsd:a` 0x1015 do not prevent gameplay. DK/OOT pctl 0x1015 is shared and not proven fatal. No game fix implemented in this log-review pass.


### 2026-11-23 — Targeted guest execution, synchronization and NVN stall diagnostics
- Following five-game hardware comparison (Starfox working; DK folder/NUP stalled idle storage; Zelda OOT zero-frame captures), prioritized guest-thread execution/synchronization/graphics rather than more speculative RomFS read-size tweaks or blanket kernel restrictions.
- Source commits `7954b5f930394373b16c2dae3a534bd47601e6e8` and `d45d3a699dab1e1c8c9019dba5c0a39c4269a74c` in `probes/game-launch/source/main.c`.
- **Reduced logging overhead:** default persistent IPC mode is restored to sparse events (service/FSP decisions, native errors, locally rejected/unsupported requests); full metadata IPC remains in bounded RAM/crash tail. Opt into high-volume text only by creating `sdmc:/switch/BackupLoaderMKW/full-ipc-trace.flag`; optional `sparse-ipc-trace.flag` takes precedence if both present. Explicitly do not claim lossless full history: finite IPC ring can still drop high-rate events; `IPC_EVENTS_DROPPED` measures observer loss only. Existing `origin=REQUEST/KERNEL_RETURN/GUEST_VIRTUAL_FS/LOCAL_UNSUPPORTED_REPLY/LOCAL_REJECT` remains.
- **Repeatable no-present triage:** once 30 actual native PresentTexture returns occurred, 8-second gap triggers `WARNING_SUSPECTED_PRESENT_STALL` and repeated every 10s while unresolved (rather than only once). Adds `WARNING_STALL_NVN_STAGE` for submit/flush/finish/window_acquire/queue_acquire entered/returned/result/observed and `WARNING_STALL_GUEST_EXECUTION` for main_entered, main_returned, IPC begin/end, sync-flight collision count, storage-active slot. Existing `report_unresolved_waits` includes outstanding native synchronous IPC flight TLS/caller/handle/age and storage. Mode A only instruments Present; `observed=0` on other NVN stages explicitly prevents misreading zeros as inactivity. Fully instrumented ZR+A already observes those methods but may change game timing.
- **Zero-present triage:** for guest launch stage 10 with zero frames, emit `WARNING_ZERO_FRAME_GUEST_PROGRESS` plus `report_unresolved_waits` every 12s after startup. This provides a bounded witness for OOT without declaring a load screen a deadlock.
- **Noninvasive:** no guest IPC reply changes, no save namespace migration, no new hooks, no preemption/fake GPU progress, no forced timeout/disconnect. Preserve Starfox/Yoshi. Code inspected for correct string escaping/source consistency; no local Switch 2 native toolchain / console hardware test by assistant. This is diagnostic infrastructure, not a demonstrated DK freeze/MKW close fix.


### 2026-11-23 22:43 — DK Bananza menu-to-gameplay boundary (NUP)
- User explicitly confirms full menu navigation/configuration works. Loading an existing save **or** starting a fresh save freezes precisely when gameplay/world begins. MKW host later exits with software-error screen. User suspects graphics and possible relation to Yoshi's in-level freezes; these remain testable hypotheses, not a proven shared bug.
- 22:43 NUP pair `crash(20261123-224338)` / `nup(20261123-224338)`: final native Presents=3902/3902; ROM reads 6071/6071; RPC 6193 done/0 pending; observed IPC 71631/71631; in-memory IPC ring 143385 events, 0 dropped. Repeated no-present warnings 8–98s. Worker phase3 sleep `poll_loops=541611` stalls with `last_poll_age_ms` increasing to ~98s. Guest main not returned; no captured CPU fault. NVN submit/etc counters 0/0 **because A mode does not observe them**. Sync-flight collisions=364 means absence of pending native waits isn't conclusive.
- Prior DK folder stalls stuck in native file OPEN/READ are distinct observed modes and must stay documented. Prior DK idle-storage cases and latest NUP gameplay boundary increase priority of graphics + thread synchronization study over file chunking. No new fix or verified root cause in this observation.


### 2026-11-23 — Targeted DK gameplay GPU trace build
- Selected all-games opt-in GPU call boundary tracer following DK world-entry-only failure (menus functional). Normal A without SD flag keeps working baseline. Create `/switch/BackupLoaderMKW/gameplay-gpu-trace.flag` on SD and launch with **A** to activate 5 exact-ABI native NVN queue/finish/flush/acquire wrappers + existing Present observer. `WARNING_STALL_NVN_PROVENANCE` during DK freeze contains enter/return differences, caller and thread TLS for last stage invocation; correlate with `WARNING_NATIVE_SYNC_IN_FLIGHT` and `WARNING_WORKER_LIVENESS`. Testing Star Fox without flag before opt-in DK isolates regression risk. If target freeze happens only with flag, flag-induced timing remains possible. No hardware validation or gameplay fix yet.


### 2026-11-23 — Follow-up source safety audit for gameplay GPU probe
- Added `#include <switch.h>` in `bridge.c` (`0894be654304183f8f3cd7858afaa52517f37d45`) for `armGetSystemTick()`; fixed `WARNING_STALL_NATIVE_PRESENT nvn_other_stages_observed` to count opt-in A-mode GPU tracing (`a1a8b75c50a09f5c6d98119a96405fa640fd452b`). Added a zero-timeout `svcWaitSynchronization` on the guest main worker handle during confirmed suspected-Present stalls (`74c381a3c50b1cdc2a6e3fed2afc3c75e1f8bd2c`); `WARNING_GUEST_WORKER_TERMINATION_PROBE terminated=0` means worker handle not signaled, NOT runnable or definitely deadlocked. Never wait on hardware worker synchronously with nonzero timeout.
- GPU diagnostic remains a user-opt-in file flag plus A mode; unflagged A and all gameplay paths preserved. Test both if any regression. Source inspected for consistent symbols/format strings, but **no local Switch 2 build or runtime test** performed in the assistant environment. Hardware may reveal a compile or ABI issue; do not claim otherwise.


### 2026-11-23 23:01 — DK Bananza ZR+A GPU trace
- Correction: ZR+A correctly selected full NVN tracing (`mode=0 diagnostic_full`); `GAMEPLAY_GPU_TRACE enabled=0` is separate A-mode-flag state, NOT evidence tracing was inactive. Uploaded NUP+crash 23:01: 307 Presents returned, 308 window-acquires returned, submit 921/921, flush 1531/1531, finish 1/1, queue-acquire 0/0. No wrapped GPU call observed stuck inside itself at stall. Last acquired texture may be awaiting subsequent presentation, but aggregate counters cannot prove precise ordering or causality. Guest RPC 2097 completed, none pending; native IPC 8633/8633. Guest main thread handle did not terminate, still no observed work for >=28s; storage worker phase3 poll loop stopped. Focus next probe in frame interval acquire→present on uninstrumented NVN calls/thread synchronization; test +1 count as baseline on working Starfox before calling it abnormal. Full sparse logs 0 dropped. No fixed cause and no code fix pushed.


### 2026-11-23 — Per-thread GPU acquire→present sequence probe (new executable code)
- Implemented on `experimental-nup-support`: `fa2620b7af725bffb6e99bb5b82bc0792d7fcb38` in `probes/game-launch/source/launch.h`; `19373722f0b88870577bae074aa831defa75e69d` in `probes/game-launch/source/bridge.c`; `7a43c84ba0f2c4701507ab5bb1d5cd7c34464082` in `probes/game-launch/source/main.c`.
- Prior confirmed ZR+A Bananza trace: 308 completed window-acquires / 307 completed Presents, submit 921/921, flush 1531/1531, finish 1/1, no pending tracked ROM or IPC. Counts alone cannot prove ordering/thread identity. New bounded 256-entry `Nx2GpuBoundary` RAM ring records entry/return **in one chronological sequence**: kind 10/11 PresentTexture, 20/21 SubmitCommands, 22/23 Flush, 24/25 Finish, 26/27 WindowAcquireTexture, 28/29 QueueAcquireTexture. Each entry includes atomically committed sequence, system tick, TLS, caller, queue/window addresses, and texture index where known. Host reports up to 64 recent events as `WARNING_GPU_BOUNDARY_HISTORY` and `WARNING_GPU_BOUNDARY_EVENT` at 8-second present stall, then every 10 seconds; event ages are ms. Compare TLS and queue/window to test whether the +1 acquire and next missing Present belong to the same thread/device. **Do not infer actual GPU fence wait from an absence of a boundary event.**
- No extra GPU calls, no changes to original NVN arguments/return values, no guest-side SD writes or RPC, no forced unsticking or new service stubs. Timeline active only in ZR+A diagnostic full mode or in opt-in A with `SD:/switch/BackupLoaderMKW/gameplay-gpu-trace.flag`. Plain A and native L+A retain prior graphics execution behavior. Existing native sync-flight/TLS/age and zero-timeout worker termination reports remain alongside these events for guest synchronization context; these capture **observed native IPC waits only**, not kernel scheduler state or SDK userspace mutex/condition/fence waits. Hardware build/run NOT performed in assistant environment. If event sequence diverges across threads, avoid false acquire/present causal linkage. Preserve all prior findings and saves.


### 2026-11-23 — FIX ATTEMPT: One-shot CPU-affinity recovery on native Present stall
- Code commit `66a0be1752e47d587928994d74995bab0f119d93` in `probes/game-launch/source/main.c`. This is an actual behavior change to try to recover game progress, **not merely diagnostic instrumentation**. Current guest worker and storage worker are each created with a one-core affinity. At the existing native-Present stall threshold (>=30 completed Presents, then no new Present for >=8 seconds, A or ZR+A), the watchdog now performs **one** kernel-permitted `svcSetThreadCoreMask` attempt to broaden guest and storage worker allowed CPUs while preserving the original preferred CPU for each and excluding the watchdog core/other worker preferred core where possible. This allows runnable threads to migrate if single-core scheduling or a shared-core dependency prevented progress. No thread is recreated, no game-owned files are modified, and **no GPU, IPC or FS operation receives a fabricated success**. The original kernel worker handles remain unchanged. Kernel failures are logged and existing affinity remains. No repeated recovery loop or forced thread kill/restart.
- The recovery result is `WARNING_SCHEDULER_RECOVERY` in NUP and crash logs, with handle, preferred core, new mask, result code. If frames resume after the rebind, that suggests scheduler contention; it is not proof until repeated controlled tests. If frames do not resume, a real GPU synchronization wait, userspace lock, or other cause remains plausible. The observed `kernel_result=0000ea01 terminated=0` from a zero-timeout `svcWaitSynchronization` check only indicates a **living** worker; this code intentionally does not treat 0xea01 as an error requiring retry. Real physical kernel CPU mask read from `InfoType_CoreMask`; on query failure, skip recovery. The change only triggers after a stall in observed Present modes; L+A fully native remains unchanged. Build/hardware test still required on user's local Switch 2. Preserve prior findings; default A & ZR+A get the one-shot recovery.


### 2026-11-23 / 24 — DK Bananza gameplay reached; staged affinity recovery fix attempt
- Uploaded test files: `BackupLoaderMKW-nup.txt`, `BackupLoaderMKW-crash.txt`, from build with one-shot affinity recovery. User observed freeze during loading, recovery, then reached **in-game**, before second freeze. Logs substantiate: initial 8s present stall at **2,589 frames** followed by accepted guest+storage affinity change (`guest_rc=00000000`, `storage_rc=00000000`, masks `0x0f` and `0x2e` respectively). Present counter later advanced to **4,063 frames** — **1,474 further** completed presents after recovery. This is strong correlation but does not prove the affinity change was causal. Second stall at 4,063 frames persisted over at least 40 seconds; 7,722 completed RPCs, no pending requests; 80,223/80,223 guest intercepted IPC; guest main alive/not returned; no captured CPU exception. **Storage worker remained actively polling** (phase 3, loop_count advances in snapshots and last_poll_age_ms=0), so prior narrative that it permanently stopped polling does not apply to this run. The source of the second stall is not established. Launch was regular A mode (`nvn_mode=1`, `GAMEPLAY_GPU_TRACE enabled=0`), hence ordered GPU boundary timeline not collected; do not infer last GPU call.
- Root behavioral limitation identified: previous code used a single `affinity_recovery_attempted` boolean and never attempted recovery on a second distinct stall, even after video resumed. **Executable fix attempt** `70411ea3495039648aeab7b2941c7b8fcabf2441` allows up to **two distinct recovery stages**: at first >=8s stall after >=30 completed Presents, broaden guest/storage affinities to spare permitted cores as before; once fresh Present progress is observed, arm stage 2; at a later >=8s stall, broaden both workers to all kernel-permitted cores. Do not repeat during the same stall or after stage 2. `WARNING_SCHEDULER_RECOVERY recovery_stage=1|2` reports all syscall results and masks; `SCHEDULER_RECOVERY_PROGRESS` marks that post-stage-1 rendering resumed. Does not fake native GPU/IPC, kill workers, or change saves. Does not prove a cure; requires local build and console test. Native L+A bypasses the Present-based stall watchdog/recovery.


### 2026-11-23/24 — Folder hot-file cache experiment; DK/DragXDrive source comparison
- User reported DK Bananza **folder** loads very slowly from conventional SD card but reached gameplay (running and attacking), followed by image freeze and buzzing audio. NUP faster for DK, while Yoshi and Star Fox NUP slower than folders. SD Express purchase is NOT required to run this experiment; no forced external hardware.
- Inspected DK folder logs `dk folder -gameplay.txt` and `dk folder - crash.txt`: folder already had `ROMFS_CACHE_HIT` for **75,238** files and 12,580,592 bytes of index metadata; content indexing took **272ms**. The slow part is later asset data loading, not necessarily enumerating RomFS. Stage-1 affinity expansion accepted and rendering resumed at 2,054 frames; subsequent count reached **8,731**. Stage 2 accepted but failed to restore rendering after in-game stall. At final stall RPC=7,742, ROM bytes=725,621,930, pending storage=0, GPU Presents=8,731/8,731, IPC=169,194/169,194, no CPU fault detected. Audio buzz could be repeated audio buffer when the game stops progressing but is NOT proof of SD cause.
- DragXDrive NUP logs `dragXdrive-nup.txt` and `dragXdrive.txt`: stall at 1,265 returned Presents with 1,266 entered; the extra in-flight Present is notable, unlike DK's returned/entered equality. No pending storage at final snapshot; stage-1 affinity operation accepted but did not restore frames. Avoid assuming all games are stuck in the same code path.
- The repository already implements persistent **RomFS metadata index caching** (`ROMFS_CACHE_HIT`) and demand-first four-window sequential prefetch of 512 KiB each. Neither addresses repeated small random reads of loose files. New **runtime optimization**, commits `7ebfe8e706532eb6eeb82f853daac852cebffc52`, `1b2081748d910e5e294e7543ccfa5000d0508fa6`, and `973f706f37ca804ca61b4259e48dc4d78ebeb16d`: per-launch **8-entry/32-KiB per-entry hot-file data cache** for folder RomFS only (max 256 KiB data). Tracks 64 direct-mapped first-access identities; only when a file <=32 KiB is requested again and mapping remains tracked, a single complete read populates the LRU. Cache hit returns the exact requested byte slice; original normal read behavior/error reporting applies on cache failure. No boot-time game-wide prefetch, no persistent asset writes, no compressed NUP changes, and existing 16-open-file handle cache and sequential prefetch remain. `FOLDER_HOT_CACHE_STATS` logs hits/fills/fill_failures and prior sequential prefetch counters on stall so impact is measurable. Hardware compilation/performance unverified; user pulls the experimental branch and builds locally.
- Priority next: compare same-title cold/warm folder loading with/without cache, assess actual cache stats and stalls. Since DK/Drag final freeze had no outstanding ROM requests, cache cannot be claimed to fix GPU/audio stall; use ZR+A graphics boundaries and consider host stall/guest sync root causes separately. Preserve working Yoshi and Star Fox, no stubs/fabricated native success.


### 2026-11-23 / 24 — ZR+A preventive scheduling fix attempt after GPU boundary test
- User supplied ZR+A DK Bananza folder crash/gameplay logs. GPU boundary timeline: queue submit entered/returned 14,877/14,877; flush 24,791/24,791; finish 1/1; window texture acquire 4,960/4,960; present 4,959/4,959. At stop there is one more acquired texture than completed Presents, but **no intercepted NVN operation remained inside its wrapper**; the game may be waiting in code not yet intercepted (e.g., guest-side synchronization or another graphics API), and no cause has been proved. Stage 1 affinity accepted but never restored rendering. User wants actual fix attempt, not only more tracing.
- **Code commit `03ddba2e0c587d87c9bc7c2863152c046b6e382b`**: for ZR+A (`nvn_profile==0`) only, execute the existing kernel-validated stage-1 affinity expansion for guest and storage threads **immediately after launching the workers**, before the game has time to enter its later graphics stall. Label `SCHEDULER_PREVENTIVE_POLICY mode=ZR+A stage=1` plus the actual `WARNING_SCHEDULER_RECOVERY recovery_stage=1` and syscall results. A and L+A modes are unchanged. Once rendering progress is observed, stage 2 can re-arm on a genuinely new 8-second stall; no repeated recovery in the same stall. This tests prevention versus too-late post-stall rescue; it does not force fences or stub GPU results. If an early freeze occurs before 30 Presents, normal watchdog threshold still applies. **No hardware compilation or run completed by assistant.** If preventive affinity makes no difference, focus on capturing guest-thread wait/synchronization site rather than repeatedly widening CPU masks.


## 2026-11-23 — DK Bananza NUP extended Artist gameplay, return-to-title freeze; architecture roadmap and safety decisions

### New hardware report (user test; source logs `BackupLoaderMKW-nup(2).txt` and `BackupLoaderMKW-crash(4).txt`)
- User selected DK Bananza **NUP**, reached the title screen, entered **DK Artist**, broke a rock and painted with mouse controls for an extended session. In-game save confirmation was displayed. On selecting **return to title screen**, the game froze. This is notably later than previous loading-screen/early-game freezes. Confirm persisted save by a separate later reload; a displayed confirmation alone is not post-reboot verification.
- Exact source: guest `040074a01bf12000`, host MKW `0400c3f00006e000`, build prints `experimental 0.15`, `NVN_LAUNCH_MODE profile=native_present_observed mode=1 selected_by=A`. **This was A mode, not ZR+A**; thus the earlier ZR+A-only preventive affinity experiment was NOT under test.
- NUP index: **75,238 files**, **9,158,656,000 virtual bytes**, **12,580,592 index bytes**; content ready ~1.85 s; prepared modules at ~4.179 s; guest worker launched ~4.509 s. The NUP file launched quickly relative to prior extracted folder reports.
- Final present counts: **14,459 entered / 14,459 returned**, no outstanding traced Present. Frame count stopped at 14,459 around ~260 s after launch; logs continue to ~318 s with a persistent stall (8 s, then 40.977 s and 50.977 s warnings). **No observed frame recovery.**
- **494,020 IPC events captured, consumed 494,020, dropped 0**, corresponding to 247,010 paired IPC requests/replies. Final crash report has **246,917** paired calls at most recent Present and **148 later IPC events**; don't confuse later IPC activity with rendering progress. Last tail includes `appletOE` requests with returned kernel/CMIF success, plus an unnamed handle event; `appletOE` appearing in a tail is **not evidence it caused the freeze**, nor permission to probe it experimentally.
- Storage: **2,521 total RPC operations**, **384,802,730 RomFS bytes** (~384.8 MB decimal), `GUEST_FS_READ entries=2383 returned=2383`, final read `last_result=00000000`, pending storage 0, service worker `running=1`, heartbeat/poll loops advancing. These show no *observed outstanding request*, NOT proof of continued underlying SD integrity or absence of prior disconnect/latent filesystem failure.
- Save path evidence: `PRIVATE_SAVE op=10` and `op=13` for `/config.sav` plus multiple `PRIVATE_SAVE` operations returned `00000000` around elapsed 256-258 s (~14,000 Presents), consistent with user's successful-save report. Do NOT label save as proven durable until reload verified.
- Scheduler: watchdog invoked stage-1 affinity recovery **after** stall at ~8 s; guest mask `0x0f` and storage mask `0x2e`, both kernel results `00000000`; still no frames. This does **not** test the ZR+A preemptive policy. `WARNING_GUEST_WORKER_TERMINATION_PROBE ... terminated=0`, `fault_state=0`, no observed CPU exception; in-process observer cannot guarantee catch of host-level fatal faults.
- NVN profile A only traces native Present; `submit/flush/finish/window_acquire/queue_acquire=0/0 observed` reflects **disabled instrumentation**, NOT evidence those calls never occurred. Compare a future ZR+A test only as controlled experiment.
- **Priority diagnosis:** transition-specific hang at DK Artist -> title, with potential graphics lifecycle, resource teardown, sync/wait, guest applet state, or game logic involvement; no proven root cause. Target **scene-transition synchronization and worker wait-site evidence** rather than indiscriminate service stubs, global GPU fixes or assuming SD at fault.

### Cumulative architectural discussion (preserve this for next threads)
1. **Current known compatibility:** Star Fox and Fast Fusion reportedly run from MKW host; Yoshi often works but sometimes freezes; DK Bananza has now reached title and extended Artist gameplay using NUP. Work toward reproducible differences across *Star Fox, Fast Fusion, Yoshi, DK Bananza* rather than treating every failure as universal.
2. **Native isolated game process goal:** MKW is merely the entry point for NRO/hbmenu; user wants a selected game (e.g. Pokopia) launched in **its own Horizon process, using game-specific validated identity, service policy and resources**, not the host MKW identity. Simply authorizing a launch from MKW does not grant the guest permissions. Horizon loader/PM, NPDM ACI0/ACID, FS/SM registration and resource limits must be understood. Independent process may require changes to kaiHEN/privileged broker; **not implemented or proven feasible** on Switch 2.
3. **Read-only static inspection and cautious runtime probes:** Analyze available kaiHEN/libounce source/headers/binaries before service experiments. Previous speculative AppletOE/prober stalled host and forced HEN restart; **do not invoke unknown process-manager, AppletOE or other live services just because they can be opened**. No native launch probe has been implemented/pushed. Existing `R+A` CodeFS preflight is read-only in intended API behavior but cannot prove process creation. Existing `ZR+A` remains diagnostic NVN + preventive affinity; do not silently repurpose it without preserving an alternate accessible diagnostic mapping.
4. **User's exact NAND safety rule:** user **allows runtime memory changes, new threads/processes and running guest code**; forbids **intentional NAND writes/install/registration changes**, asks for backups, save files, logs and configs on **SD**. Note Horizon itself may write internal bookkeeping outside our direct calls: a *guarantee of zero NAND writes* is NOT possible without identifying/enforcing all indirect side effects. Persistent save used to re-enter kaiHEN must remain untouched.
5. **Exploit/recovery:** user's semi-untethered `s8n/CivSwitchFS-kaiHEN` branch `slop` installs a persistent Civ VII save chain. After reboot user can relaunch Civ VII -> Load Game to reinstate kaiHEN **without PC**. HEN persists until restart. Repo warns exploit can corrupt fsp-srv state/NAND; no need to trigger it from launcher. Documentation explicitly says race/canary timing is fixed in `src/core/config.mjs`, *not* an automatically persisted trained schedule.
6. **Direct launcher shortcut:** proposed hold L on game launch -> NX2BackupLoader, hold R -> existing hbmenu. Could be a future kaiHEN/HBL shortcut, but does **not** change MKW host process/permissions by itself. No changes authorized or implemented.
7. **Hybrid native ARM64 + Horizon HLE architecture:** Run guest ARM64 directly on Switch 2 CPU; use real GPU/NVN and Horizon where compatible; use coherent guest service routing, virtual SD-backed RomFS/saves, handle/identity/synchronization semantics and **real behavior** for HLE services. Prefer correct native forwarding, then faithful HLE, else explicit unsupported error; **do not invent successful stub responses**. Kernel SVCs require special handling since native instructions enter real Horizon; virtual identity cannot grant real kernel capabilities. Separate-process research continues in parallel.
8. **Immediate sensible HLE step (not yet implemented):** unify service capability accounting and compact per-game summaries from existing IPC RAM-ring capture, recording native/virtual/local reject/unsupported and result codes. Compare working titles vs DK transition; preserve A and L+A behavior. Avoid giant unfiltered logs, new live service probes, and speculative changes. Track context-specific timing, not merely final request counts.
9. **Switch 1 support:** extracted ExeFS/RomFS + metadata first, then NSP parsing/decryption/content selection after an extracted title boots. Shared ARM64/RomFS components may be reusable but older Horizon service ABI, graphics, process policy and backward-compat support are nontrivial. Keep this parallel, not at expense of Switch 2 regressions.
10. **Future per-title mods:** SD-backed `mods/<titleID>/<mod>/romfs/` layered asset overlay; default off; deterministic priority; no destructive modifications to original backups. Loose RomFS overlay straightforward; embedded internal archives (e.g., Star Fox `romfs/data.zip`, reported VZIP) require per-format understanding; replacing entire archive differs from replacing assets inside it. Later UI mods toggles, possible complex ExeFS patching.
11. **Storage observation:** user's Samsung EVO 256GB A2 transfer ~8–10 MB/s, SanDisk Extreme V30 ~80 MB/s via user's transfer path (~8–10x). Transfer performance does not prove in-game latency/throughput, and pending=0 doesn't exclude earlier lost SD connection. Controlled same-build/card comparison is useful. The card was formatted exFAT with consideration of 128 KiB clusters; extracted games' huge small-file counts may waste space.

### Follow-up targeting based on this run
- First verify saved `/config.sav` or Artist data survives a new DK NUP launch, **without rewriting or destroying the original**.
- Compare repeated normal gameplay/Artist time against reproducible return-to-title freeze, perhaps test leaving minigame in a different path; log only if user chooses.
- Focus on the moment rendering stops: snapshot guest waiting thread/handle and graphics lifecycle around return-to-title, and distinguish `appletOE` activity that successfully returns from genuinely blocked calls. No unapproved high-risk service probes.
- Existing README/Findings/HANDOFF histories are cumulative; append, never replace; no hardware compile/run performed by assistant in this update. 

### 2026-11-23 correction — DK Artist is NOT main-world gameplay
- User clarified the extended, interactive DK Bananza NUP session was **only the isolated 3D Artist/paint minigame**: one 3D apple/object onscreen with painting/mouse controls and lighting options. **No DK traversal, smashing objects in a crowded world, or full-world gameplay was demonstrated.** The original shorthand “extended gameplay”/“broke a rock” should be interpreted in the narrower sense of Artist mode interactions, NOT proof of main-game stability.
- Valid conclusions: game reached title, Artist mode rendered and accepted inputs for an extended period; in-game save confirmation appeared; freeze occurred on exiting Artist/returning to title. This tests a lighter rendering/interaction workload; cannot establish main-world physics, streaming, crowded-scene GPU workload, broad asset access, or full-game stability. Scene transition still warrants scrutiny. Saved data persistence remains unverified until reload.


## 2026-11-23 — DK Bananza NUP immediate black-screen freeze after Artist success (uploads nup(3), crash(5))
**Critical corrective evidence:** Same user, same DK NUP and **A native-present** mode, subsequent relaunch black-screen frozen almost immediately. Earlier Artist-only session (14,459 Presents; successful save UI) demonstrates a reachable code path, **NOT reproducible boot or general gameplay stability**. Do not infer a stable game runtime from one good run.
- Source logs: `BackupLoaderMKW-nup(3).txt` (1,440 lines), `BackupLoaderMKW-crash(5).txt` (1,073 lines). Same guest ID `040074a01bf12000`, host `0400c3f00006e000`, launcher `experimental 0.15`, `NVN_LAUNCH_MODE profile=native_present_observed mode=1 selected_by=A`; **not ZR+A**.
- Fast initial startup was normal: indexed 75,238 NUP RomFS files (~1,293ms), content ready ~1,850ms, prepared modules ~4,189ms, worker ~4,489ms. Module patches mapped OK. NVN bootstrap=2/getproc=1396. But only **2 Present calls (2 entered/2 returned)** throughout the later 100-second observation. Native submit/flush/etc tracing not enabled: their 0/0 counters are not evidence of missing actual NVN calls.
- **Strongly actionable new finding: storage RPC actually stuck, unlike earlier Artist exit run.** `GUEST_FS_READ entries=1891 returned=1890`; `RPC_PENDING` has two outstanding operations: slot 1 `op=1` (ROM_READ) `ticket=1986`, slot 0 `op=19` `ticket=1987` (another queued operation). `STORAGE_SERVICE_HEARTBEAT running=1 active_slot=2 active_op=1 active_ms` grows past 80s while completed count stays 1,985 and poll-loop count stays 26,204. Guest waiting on outstanding storage can explain render startup stopping.
- `WARNING_ROM_READ_STAGE phase=5 file=28520 offset=851968 bytes=851968 path=Sound/Resource/StationedPlayer.bars.zs` persists across 4.5–84.5 seconds. `NUP_STORAGE_PROGRESS file=28529 codec=0 phase=2 request=851968 sd_bytes=458752 decoded=0 delivered=0` stays unchanged for at least ~84.5 seconds. The different file indices reflect distinct index systems; NUP and RomFS IDs must not be directly equated. Both refer to the same in-flight request context insofar as active storage RPC shows the path; investigate exact mapping before assuming.
- Source `probes/game-launch/source/nup.c`: `nx2_nup_read()` on codec 0 calls `nup_seek_file()` then `read_exact_chunks()`, which marks **phase=2** and performs repeated `fread(..., 1, min(64KiB,remaining), mounted.f)`. `sd_bytes=458752` is **7 × 65,536-byte completed fread chunks**, then progress stopped. This suggests the **next 64KiB fread may be blocked** (or execution stopped before its progress increment); this is not evidence of an archive decompressor fault: codec=0, decoded=0 expected. It does not yet prove SD disconnection, card defect, or single-thread lock cause. A read of `Sound/Resource/StationedPlayer.bars.zs` appears at the stuck point; preserve game asset, no deletion.
- Last `WARNING_ROM_BACKEND_PROGRESS completed=0 chunk_bytes=0 last_result=00000000` is *no completed backend transfer*, **not successful I/O**. Last completed guest read result 0 likewise is prior success, not this pending read.
- IPC `observed_events=7546 consumed_events=7546 dropped=0` and stable 3729/3729 at last snapshots; `appletOE` IPC appears completed (kernel/CMIF 0), not proven freeze cause. `fault_state=0` means no captured in-process exception, not guaranteed no crash elsewhere.
- **Diagnosis priority changed:** investigate blocking SD/NUP `fread` and its interaction with SD-backed logging, card/reader access and concurrent operations; do not prescribe graphics-first fix for *this* black-screen run. Test storage health and read outcome without new AppletOE/process-manager probes. Compare same NUP / launcher on both microSD cards only if convenient; user reported SanDisk Extreme host transfer ~80MB/s versus Samsung EVO ~8–10MB/s but **card used in this run is not established**. It would be unsafe to claim it was a specific card.
- Proposed next code experiment (not applied): low-overhead progress before/after each native archive `fread`, offset and elapsed time on active chunk; optional SD-only test harness on PC with hashes/read stress; avoid flooding SD logs or concurrent writes. Consider exclusive archive FILE ownership, bounded queued operations, and robust retry/recovery **only if a native read actually returns**, because a permanently blocked fread may not be cancellable by a user-space timeout.
- All game saves, logs, test content must remain SD-backed; no deliberate NAND writes/installer, no live risky service probes. Preserve prior Artist result as an observation, not general stability evidence.


## 2026-11-24 — Content resolver fix: Pokopia split base/update folder and multi-package NUP selection
- User reports Pokopia extracted layout: `Pokopia/base/exefs/` (no base RomFS), `Pokopia/updates/655360/exefs/` and `Pokopia/updates/655360/romfs/` (some dumps may name parent `update`). Current launcher rejected a base without `romfs`; prior NUP probe rejected first incomplete runnable package before evaluating later full update. User saw `no runnable pkg 0`. These are proven limitations of previous source, not evidence of kernel resource shortage.
- **Implemented on experimental-nup-support**: `probes/game-launch/source/folder.c` now discovers complete numbered update roots in `update/` or `updates/`, selects highest numeric version containing `exefs/{rtld,main,sdk,main.npdm}` and `romfs/`, and exposes that update root through the existing unmodified folder module loader, RomFS index/cache and SD-backed filesystem. Also accepts unversioned direct `update/exefs+romfs`. Falls back to a complete base when no complete update is available. `folder.h` adds `update_version`, `main.c` labels selected content root in menu/log. No original files copied, moved or changed.
- `probes/game-launch/source/nup.c` no longer rejects a partial base pkg immediately. It validates package entries, then selects last **complete update (kind=1, ExeFS+RomFS)** when available; otherwise a complete base (kind=0), otherwise a flagged standalone complete update (kind=1). The mount path uses exactly the same selection rule. For non-standalone updates, preserves first base package title ID for saves/identity. `main.c` logs `NUP_CONTENT_SELECTION` so the selection is visible.
- **IMPORTANT SCOPE: THIS IS NOT A GENERAL DELTA-UPDATE MERGER.** It selects one internally complete update ExeFS+RomFS filesystem; it does not overlay files from incomplete update RomFS onto base RomFS or synthesize a game view from disjoint partial package files. Nor does it apply Nintendo NCA/BKTR patch semantics. If all package candidates are partial and no complete package exists, NUP reports `No complete runnable base/update ExeFS+RomFS package` instead of fake successful merge. A complete-looking update RomFS that omits base assets will still need a future union/patch resolver.
- Existing A, L+A, ZR+A, B+A, R+A modes unchanged; no new Horizon service probes, no installer, no intentional NAND write. SD-only save and log behavior retained. **Build/run on Switch 2 not verified by assistant** (no local libounce toolchain in this environment). User should pull latest branch, run existing build.bat and test Pokopia extracted folder first; verify menu `Using complete update version 655360`, then launch log `CONTENT_ROOT_SELECTED path=.../updates/655360 resolved_update=1`. For a multi-package Pokopia NUP, verify `NUP: Ready: update pkg N` and `NUP_CONTENT_SELECTION result=NUP mounted update pkg N`. If test still fails, inspect code-loading and RomFS logs; don't infer NUP builder validity without its actual TOC.
- Preserve all previous DK Bananza SD/NUP intermittent storage blocking evidence, scene transition freeze and HLE/isolated process research separately; this update targets content discovery/selection, NOT DK freeze causes.


## 2026-11-24 — User-authorized experimental NSO final-segment EOF fallback for Pokopia
- User requested a trial despite recurring crashes, with Git rollback available. Source: uploaded extracted `main(5)` and Pokopia NUP logs `BackupLoaderMKW-nup(4).txt`, `BackupLoaderMKW-crash(6).txt`.
- Installed backup package selection now succeeds: `NUP_CONTENT_SELECTION result=NUP mounted update pkg 1 (complete ExeFS+RomFS)`; NPDM and `rtld` processed; `main` fails before executing guest code.
- Header and file length from extracted `main` match NUP log: 29,404,104-byte NSO; segment 2 starts 28,645,443, declares packed length 759,641, end 29,405,084, i.e. **980 bytes beyond EOF**. Flags `0000003f` (compressed/hashed segments). Original validation properly rejected out-of-bounds read; it was not a kernel/gameplay crash.
- Experimental code change `probes/game-launch/source/nso.c`: if and ONLY if the final (segment 2) is compressed, current offset exists, missing packed bytes <=4096, substitute **only actual available file bytes** as input length. DO NOT allocate synthetic bytes, pad, seek past EOF, bypass LZ4/ZBIC decoded-length checks, or skip the existing decoded SHA-256 validation. Failure continues to return a descriptive `PREPARE main status=NSO segment 2 decompression failed...`, without executing guest. All other malformed extents still reject.
- This is an **experiment, not a proven executable fix**. Offline stock `lz4.block.decompress` attempt on the uploaded NSO segments did not decode; that is suggestive but not definitive for the project's custom loader. Since the compressed stream may really be truncated or a variant, expected behavior may still be clean failure. If it moves to hash failure, the compressed segment was decoded to expected size but did not match header integrity; do not disable hash checks. If it succeeds, confirm log `PREPARE main status=OK`, then investigate next stage separately.
- Pushed experiment commit `208f124072c4cd80875c3e5d3cd0d6e73d4b4b78` on `experimental-nup-support`; prior branch commit immediately before this code modification was `fab3ecb44219c09048ca5c5717045f4af941780e` (documentation HEAD at end of previous requested fix; other earlier code commits may precede it). Roll back code commit `208f124` selectively if necessary rather than overwriting the cumulative findings.
- No console compile or hardware test performed by assistant, no NAND modifications, no live Horizon service probes, and no previous successful paths intentionally altered.


## 2026-11-24 — Sports Resort Switch 2 FSP command 202 compatibility attempt + Pokopia new NUP logs
- User supplied `BackupLoaderMKW-crash(20261124-070133).log` and `BackupLoaderMKW-nup(20261124-070133).log` for Sports Resort Switch 2, plus Pokopia `BackupLoaderMKW-crash(20261124-070456).log` and `BackupLoaderMKW-nup(20261124-070456).log`.
- Sports Resort successfully selected NUP update pkg 1, 48,576 RomFS files, 8 modules prepared and mapped (sdk mapping required successful RO session rollover), guest startup began. Log identifies `FSP_DOMAIN_REQUEST_BLOCKED` for **fsp-srv domain command 202** and an SDK break with matching unsupported result `0x00310002`. Earlier logs showed pending storage operations during startup, but the explicit blocked IPC -> error -> SDK break is strongest crash evidence. No Present completed at that point.
- Command **202 = nn::fssrv::sf::IFileSystemProxy::OpenDataStorageByDataId** (read-only IStorage). Previously fsp-srv allowlist permitted only 1,1005 and virtual save/cache routes. **Experimental fix** in `probes/game-launch/source/svc_hooks.c`: permits narrowly scoped root-domain command 202 to be forwarded to real Horizon; only if genuine successful kernel + CMIF reply registers resulting domain object as kind 4 read-only IStorage (or tags a moved handle kind 4). The new capability permits **Read command 0 and GetSize command 4 only**; rejects Write 1, Flush 2, SetSize 3 and other operations. Cloned FSP domain handles retain this restricted capability. No fabricated success; if underlying host context cannot access requested data ID, real failure propagates and may still cause SDK break. Does not grant expanded host rights. Source code commit: `858dc82d2c8eb918fef307ba79a10da0a8b4f112`.
- Important limitation: native fsp-srv still identifies MKW host title, not guest, and can refuse the requested data ID. This is a **targeted read-only native forwarding experiment, not full virtual data-id resolver** and not proof all system-title dependencies are present. Do not forward arbitrary save methods; never deliberately write NAND. Check after next test for `FSP_DATA_STORAGE_CAPABILITY_REGISTERED` and a successful native reply to 202; otherwise capture actual kernel/CMIF error. Do not mistake no SDK break for successful game boot.
- New Pokopia logs from same current branch still select NUP update pkg1, index 36 RomFS files, prepare and map rtld. `PREPARE main status=NSO segment 0 decompression failed (packed=23151272 decoded=41194624; strict hash required)`. Abort before guest entry: `PRELAUNCH_STOP phase=PREPARE_MODULES`. The prior 980-byte final-segment fallback is **not** the active obstacle here: segment 0 LZ4 decode fails before reaching it. This requires a separate accurate decoder/extraction investigation, not FSP command 202 fix. Do not disable compression/hash validation to force execution.
- No hardware build/test performed by assistant. Preserves launch modes A/L+A/ZR+A/R+A and current SD-based settings; user pulls branch and builds locally. If read-only native forwarding regresses any working title, revert code commit `858dc82` selectively; preserve cumulative docs.


## 2026-11-24 — Sports Resort memory/GPU diagnostic checkpoint (latest hardware results)

**Current executable checkpoint:** `4f0feb301cea3b1cbe0f5f772ce91963224085bf`, branch `experimental-nup-support`. User built and tested locally; latest logs contain MEMORY_OBSERVATION_EPOCH, ACCOUNTING_OBSERVATION and GPU_OBSERVATION_MODE, confirming the added diagnostic fields. No general compatibility fix or root cause established. This checkpoint supersedes older status statements where they conflict.

### Evidence and interpretation
- Earlier comparison: `BackupLoaderMKW-crash(20261124-184023).log` and `BackupLoaderMKW-nup(20261124-184022).log`, Sports Resort Switch 2, L+A full-native profile. Prior review recorded 4,382 memory events (2,191 successful returns), no unmatched entries, last complete snapshot 3,758 ms. Event 861 (entry, placeholder ffffffff) was paired with successful return 862; not evidence of a rejected mapping. Full-native GPU counters do not establish absence of GPU activity.
- Latest directly checked files: `BackupLoaderMKW-crash(20261124-184704).log`, `BackupLoaderMKW-nup(20261124-184703).log`. Title 04008e001f098000, update pkg 1, 48,576 indexed files, eight modules, A/native_present_observed mode 1, GPU tracing enabled by SD flag. Guest entered main. Initial SDK module mapping returned 0xE16 and succeeded on retry; not the terminal failure.
- 4,212 recorded memory events, 2,106 complete operations: op4 physical map 2,059; op5 physical unmap 44; op1 shared map 3. Every recorded return was zero; no unmatched entry IDs. Three shared-memory metadata samples (IDs 2, 3, 1928) report successful endpoint queries, type 6 and permission 1 matching request. Endpoint samples are not full-range/lifetime proof; successful operations do not establish subsequent access correctness.
- Fifteen accounting observations all consistent (stable total and used within total); final total 9,462,349,824 bytes, used 7,700,959,232, free 1,761,390,592 (~1.64 GiB). Host allocator total 1,074,855,936, free 753,467,392 (~718.6 MiB), minimum sampled free 733,347,840. No observed exhaustion; this does not exclude a separate resource limit or transient failure. Do not increase host heap based solely on these logs.
- Primary log has 14 GUEST_MEMORY_ACCOUNTING_CAPPED entries. This is an observed guest/host reporting adjustment, not a proven defect. Final accounting queries continue after GPU startup evidence.
- Last complete host snapshot: 3,759 ms, stage 10, fault_state=0, guest_main=1, rpc=616, rom_bytes=54,227,653, pending=1, guest_abort=0, host_abort=0, frames=0, nvn_bootstrap=2, getproc=1470, queues=5/5, window=1/1, acquire=1/1, submit=0/0, finish=0/0, present=0/0, ipc=880/880. GPU history 1,218 produced/consumed, zero pending; shared history 4,212 produced/consumed, zero pending. Queue/window results 1 are consistent with boolean success, not necessarily error codes. Device counter 0/0 is not evidence of failed device creation. GPU resource initialization and acquisition observed; no submission or presentation observed. No captured CPU exception. Last snapshot time is not proven crash time. A and L+A last snapshots differ by only 1 ms, but sampling interval limits inference.
- Earlier service acquisition pctl:s/a/r returned CMIF 0x1015; pl:u and aud:u closure results 0xF601 were previously identified as closure-context events, not established crash causes. No terminal exception or exact cause identified.

### Remaining suspects — preserve these priorities
| Suspect | Priority | Evidence / limitation |
|---|---|---|
| GPU resource use following initialization | High | queues/window/acquire observed; no observed submit or present; no direct GPU hardware fault report |
| Memory access following successful mapping, including lifetime/validity | High | no mapping rejection, but mapping success and endpoint metadata cannot establish later access correctness |
| Guest memory accounting adjustments | Medium | capped accounting observed; all sampled physical consistency checks passed; no causal link proven |
| Shared memory mapping rejection | Low | every recorded memory return successful |
| Physical memory exhaustion | Low | ~1.64 GiB sampled free |
| Host allocator exhaustion | Low | ~718.6 MiB sampled free |
| Unobserved kernel/CPU/GPU termination | Unresolved | internal exception handler did not report a fault; monitor cannot independently capture these events |

### Work completed and stability constraints
- USB experiments 06f24f7 and d33b9c0: initialization returned 0xF601 in stage one; user subsequently reported a system hang on exiting launcher after USB test. Commit 66a9251 removed Y action, menu label, probe implementation and USB host include. USB on hold, SD behavior unchanged.
- Independent crash monitor V1–V4: 1488780 foundation, e3073f8 global observations, 8279e59 cooperative evidence, fe5a08a failure-only logging, 4171e8e cursor preservation. V4 user reports four consecutive kaiHEN loading crashes with plugin installed, success on fifth attempt without plugin; status had said running. Strong evidence of incompatibility, exact cause unknown; not proof of debugger attachment (no such attachment implemented). Keep nx2-crash.nso disabled; no V5 implementation yet. User wants actionable error/exception evidence only, no routine successes or warnings, independent survival of launcher failure. Do not require another jailbreak merely to test incremental monitor changes. Direct GPU hardware and independent kernel exception capture remain unavailable.
- Commit 4f0feb3 added passive observation fields, CPU syndrome classification and offline scripts/analyze-memory-log.py; no mapping permissions, allocation or GPU execution changes. User's newest logs confirm fields present. Current checkpoint is a diagnostic update, not a fix.
- Local workflow: pull with GitHub Desktop, build.bat, replace NRO; no Actions/PRs. Keep monitor disabled and USB removed. Preserve append-only findings/handoff. Previous working Star Fox baseline remains user-reported; latest comparison tests do not independently revalidate it.
- Remaining requested comparison: ZR+A instrumented Sports Resort and successful Star Fox baseline, if available, with separate crash and primary logs per launch. No result for those tests in this checkpoint. No new executable modification or console test in this documentation update.



## 2026-11-24 — Yoshi gameplay logger volume reduction
Yoshi Full NUP user report: explicit SDK break with zero payload after gameplay, unlike Sports Resort abrupt termination. Prior review reports ~72 seconds, 2,863 frames and >50,000 crash-log lines; history gaps limit completeness. Logging overhead is a hypothesis, not established cause. This patch preserves startup detail and existing exception/SDK break/stack output. After first observed frame, memory records are aggregated (observed success/failure totals, intentional omission count), shared map records and failed returns retained plus latest 16 ring records per sample. GPU ring exports latest 16 records per gameplay sample; stage snapshots remain; intentional omissions explicitly counted. GPU ring drain limit increased to ring capacity, so startup may export more detail. Ring gaps remain separate from intentional omissions; offline range ledger is incomplete in compact gameplay. No change to game execution, memory allocation, permissions or native GPU results. Existing periodic flush policy unchanged; no guarantee of immediate capture if host dies between samples. Monitor remains disabled and USB removed. Native compilation and hardware impact not yet verified.

## 2026-11-24 - Universal compatibility attempt 1 awaiting hardware

No compatibility result changed: no new game logs or console tests in this session. Source/build-only attempt adds verified SVC wrapper variants, nonblocking routine text logging, native offset-based NUP reads, actual SD-backed save capacity and regular-session read-only FSP 202 forwarding. All 14 portable fixtures passed and native Switch 2 compilation/link/NRO generation passed. Marker UNIVERSAL_COMPAT_ATTEMPT version=1 distinguishes this build. Start with Star Fox baseline, then failing startup and gameplay-transition tests. Bananza storage-stall evidence motivates the I/O/logging experiment; no root cause or fix success claimed. Dynamic modules, partial base/update content, large fixed heap requests and uncaptured graphics failures remain open. Full details appended to Findings.md and handoff.

## 2026-11-24 - MKW extracted layout and universal union correction

Direct source-tree inspection: D:/Roms/Switch 2/Mario Kart World/base/romfs has arc and audio absent from updates/917504/romfs; both ExeFS roots exist. Previous complete-looking update selection could therefore omit real base assets. Implemented update-over-base extracted RomFS union with per-file source identities and isolated composite caches/checkpoints. Existing folder layout is now supported without reorganizing or recopying game data after transfer completion. Synthetic integration and native -Werror build passed; no console result, no SD completeness verification, no new playable-game claim. NUP package union and dynamic modules remain open. See appended Findings/handoff.

## 2026-11-25 - OOT uploaded folder logs, not current executable confirmation

Latest supplied gameplay/crash pair is Zelda OOT, cached 186,587-file folder. No UNIVERSAL_COMPAT_ATTEMPT/FOLDER_UNION_ATTEMPT/ASYNC_LOG_SUMMARY markers; host prefix ~769 MiB and canonical_program_id save namespace also differ from current source/build. Do not count this as a failed test of the prepared universal/folder-union NRO. Unknown whether old/different executable ran or earlier logs were selected.

All four modules and guest heap succeed, nnMain runs, assets continue being read and primary log ends after successful private save mount. Last complete snapshot 38.22s, ~132.8 MiB delivered, no captured exception/break/abort or presentation. A ~4.017s storage-worker warning on a 4264-byte texture read recovers; later completed counters advance. Closing aud:u 0xF601 is not the terminal cause. Clarify onscreen outcome and confirm build markers in fresh logs before another executable change. Current packaged NRO hash remains F9109B5A283A8F1120D6949FC880BCBACFAE5E9E7BA08B5BA1EDEF43E008642C (663824 bytes). Saves and code unchanged this review. Details appended to Findings.md.

## 2026-11-25 - Follow-up: defer Pokopia and verify deployed binary first

Pokopia still absent; user explicitly deferred investigation. Record screenshot only, no assumptions about hidden update contents. New OOT gameplay(1)/crash(1) logs are a distinct run, but again lack universal/union markers, retain 806490112 host bytes and canonical_program_id saves. Local packaged NRO is verified marker-bearing, 663824 bytes, SHA-256 F9109B5A283A8F1120D6949FC880BCBACFAE5E9E7BA08B5BA1EDEF43E008642C. Exact deployed NRO is unverified; SD cannot be inspected here. Check its file size/hash before requesting another console test. Last snapshot 36.605 s, ~144.4 MiB read, no captured terminal exception, successful save mount later in primary log, final IPC watchdog 1083/1083. No new compatibility outcome established and no code/save changes this review.

## 2026-11-25 - OOT current-build test confirmed, abrupt self-close

User: black screen for a while, then application closed itself. gameplay(2)/crash(2) now contain both attempt markers, 1-GiB host reservation and async logger counters, resolving deployment ambiguity. All 2754 crash lines reviewed. Last snapshot 34.425s, ~114.8 MiB assets, no frames/abort/captured exception. Main log reaches successful save-filesystem mount and 692/692 IPC watchdog. 733 complete observed native memory operations all succeeded; sampled physical free ~2.41 GiB and host free ~799 MiB. No STOP_PROCESS/SDK break/requested exit marker. Detailed GPU trace disabled, so zero GPU init/submit counters are not diagnostic. Source checks show ordinary stop/intercepted exit paths should log; cause remains uncaptured, not proven GPU or memory defect.

Next available targeted trace: empty /switch/BackupLoaderMKW/gameplay-gpu-trace.flag, launch A, verify GAMEPLAY_GPU_TRACE enabled=1. Keep monitor disabled; no build needed for this flag. OOT flat content root does not exercise folder union or NUP changes. Pokopia detection stays deferred per user. No code/save changes for this review; details appended to Findings.md.

## 2026-11-25 - GPU trace confirms OOT reaches initial submit/finish; Pokopia separate

User priority: OOT. Pokopia detection now works after correcting launcher placement; mark detection resolved. Pokopia still cleanly refuses main segment0 decompression before guest entry (23151272 packed / 41194624 decoded), so it does not share OOT's demonstrated runtime failure stage. Leave its decoder/input issue separate; no hash bypass or content rewrite.

OOT gameplay(3)/crash(3) confirm both current markers and GAMEPLAY_GPU_TRACE enabled=1. All 4508 crash lines reviewed. Seven queue inits and one window init return1; acquisition returns index0; one submit and one finish both return. 1334 GPU events fully drained. 130 pool,42 texture,9 sync init and6 virtual-map returned boolean1, none observed false. No presentation or captured terminal exception. Last snapshot35.432s,~118.1MiB assets, native memory799 successful returns, physical free~2.41GiB, host free~799MiB. Primary reaches save-filesystem mount and684/684 watchdog. Device0/0 and absent guest-called error polling are not proof of failure/no-fault.

Do not diagnose a common Pokopia/OOT cause or a blanket GPU-init failure. Investigate OOT's later startup/termination before first observed frame; current trace does not justify an allocation or decoder change. No executable modifications this review. Details appended to Findings.md.

## 2026-11-25 - Resource diagnostic build 1 ready for OOT

New marker RESOURCE_DIAGNOSTIC_BUILD version=1. Host-only snapshots now measure actual management-pool total/used/remaining and free threads; permission-hinted native quota maximum/current/peak for memory, threads, events, transfer memory and sessions; recorded errors/unavailable flags. Baselines before modules and before guest, periodic >=1s samples through existing crash cadence, forced first save-mount sample. No allocation/permission/limit changes. Resource-limit handle is duplicated/query/closed per sample; failed close prevents new acquisitions. Unhinted privileged getters are never called. RESOURCE_CHECK now says configured_total, not available.

Resource mock test plus existing SVC wrapper and folder-union integration tests passed. All34 native translation units compiled -Werror and ELF/NRO generation passed; console test pending. Preserve prior NRO under release/previous/BackupLoaderMKW-before-resource-diagnostic1.nro. Test OOT with A and existing GPU flag; send primary/crash logs. Pool exhaustion/quota proximity must be evidenced, not inferred from ordinary free RAM. Dynamic permissions/identity and uncaptured system termination remain open; Pokopia not priority. Full detail appended to Findings.md.

## 2026-11-25 - Resource pool healthy; diagnostic v2 corrects thread observation

gameplay(4)/crash(4) confirm resource v1.23 consistent samples:64MiB management pool, max sampled used1257472 (~1.2MiB), min remaining65851392 (~62.8MiB), including flushed save_mounted at37.142s. This particular pool is not observed near exhaustion. Privileged quota getter hints all0; skipped fields are unknown, not failures/zeros. Free-thread query0xe401 was an assistant diagnostic bug (INVALID_HANDLE instead of CUR_PROCESS_HANDLE for InfoType24), not proof of game thread exhaustion. Corrected query and mock; fixture passes. Type9 resource-limit query still properly uses INVALID_HANDLE. New marker RESOURCE_DIAGNOSTIC_BUILD version=2 free_thread_handle=current_process.

No captured terminal cause;805 memory returns succeed, physical/host free remain ample, initial GPU submit/finish return but no present. v2 changes measurement only. Keep OOT focus and existing GPU flag, no plugin/permission/limit changes. See Findings.md appended entry for full evidence and caveats.

## 2026-11-25 - OOT v2 hardware diagnosis: resources have headroom

Full gameplay(5)/crash(5) read. Correct v2 build, free-thread query now succeeds:299 initially, minimum248 at save_mounted37.564s.22 pool samples; max used1249280 of67108864 bytes, min free65859584 (~62.8MiB). Strong negative evidence for sustained thread/system-pool exhaustion at captured checkpoints; other privileged quotas remain unavailable.733 memory returns successful, no recorded failures; last physical/host free2589425664/837746688. Initial GPU init/acquire/submit/finish return,1334 GPU records drained, no present. Private save FS mounts successfully, logs stop after forced resource checkpoint, no terminal exception/exit captured. Do not increase budgets or claim save/GPU fault without evidence. No new executable changes from this logpair; existing v2 build confirmed. Focus next diagnosis on uncaptured post-save termination before first presentation; save contents/individual reads not yet validated. Full appended evidence in Findings.md.

## 2026-11-25 - Backup source audit: OOT integrity checks pass; Pokopia input issue separate

Read-only local source inspection completed. New scripts/audit-backup-exefs.py validates NPDM and exact NSO segment decode/hash using LZ4 or existing ZBIC implementation. OOT all12 segments pass embedded hashes; Starfox21 and Fast Fusion15 pass. OOT main is ZBIC (flags0xff), not ordinary LZ4. OOT186587 PC files equals SD index count, zero empty files, no .nro/.nrr/.nso in RomFS. Sampled startup heap/address-table/dictionary/texture assets decompress; ResidentCommon and Bootup packs decode correctly with packaged dictionary id4. Not a full SD/PC byte comparison or all-assets audit.

Pokopia base/update main/sdk/subsdk0 fail independent LZ4 checks and final data extents exceed EOF; rtld passes. This supports a source representation/integrity problem separate from OOT late shutdown. No bypass/recovery or game input changes. OOT48MiB reservation,1MiB stack,137MiB Program subheap align with previous observations. Full evidence appended to Findings.md, audit JSON in .work/source-audit. No new NRO/limit/save changes; continue OOT uncaptured post-mount termination diagnosis.

## 2026-11-25 - Experimental independent-process test build

User authorized architecture experiment. Separate probes/process-launch + build-process-experiment.ps1, unique BackupLoaderProcessExperiment NRO/menu, two SD-root process logs. IMPORTANT: first stage is tiny independent child ExitProcess payload, not game executable launch; game_launch_implemented=0 in UI/report, no in-process fallback. Confirms permitted primitive before building full child native game/filesystem/application backend. kaiHEN public protocol7 exposes no arbitrary-folder spawn; bootstrap PM launch uses separately supplied PM session. Source details/evidence appended to Findings.md and probes/process-launch/README.md.

Hints guard all privileged calls. If advertised, create own small resource object, child, distinct PID, map/write/unmap/protect code, start and watch, then cleanup. No parent quota/save/registration/HEN changes. Missing hints mean route not advertised, actual kernel permission unknown. Incomplete cleanup disables retry until title restart. Host failure/cleanup tests pass. Hardware verification pending; do not describe this as an implemented retail-game process launcher. Normal v2 NRO unchanged SHA E66A093F20BFB1132C5301C8500DB02133360ADB3330A06EE24028A05A220E73; rollback is normal menu entry. No push/commit.

## 2026-11-25 - nx2-relink reviewed; original rtld retained, remote and host-end capture added

Compared nx2-relink source and issue reports. Its per-title NRO still runs in donor process and replaces rtld; ours already executes original rtld and maps intact modules. Useful late/runtime linking and image-slot/TLS ideas require a separate nn::ro backend effort, not copying replacement-rtld globals blindly. Upstream reports do not establish same failure as ours. Full source references/differences appended to Findings.md.

Resource v3 adds raw GetInfo31/32 remote-memory fields and selected META+0x10 to existing sample cadence, distinct from management pool+0x14. PC OOT declares96MiB vs MKW192MiB, so current metadata alone shows no shortfall. Native host values/errors logged, no allocation behavior/quotas changed. Independent host termination recorder wraps five host exit/abort calls and preserves original behavior/arguments/results, dedicated pre-opened native log SD:/BackupLoaderMKW-host-end.log, bounded32 events, recursion guarded, no pointer dereference/guest RPC in logging path. Kernel/GPU/inlined-SVC terminations still may bypass.

Resource/termination/catalog integration tests pass; all35 native units compile/link -Werror, NRO format/startup validation passes; five wrapped symbols verified in ELF. Normal NRO667920 bytes SHA D01C689C1B297B2A89B62F31D290E71F76D05BAFB112728D7E5C6D028B85C436; previous resource2 kept in release/previous/BackupLoaderMKW-resource2-before-relink-review.nro. Process-test binary unchanged. Next console run: normal A and existing GPU flag; send gameplay/crash/host-end logs. No promise of launch fix; no save/UI/game data/HEN changes; no push/commit.

## 2026-11-25 - Process test hardware: no advertised creation route

Both process logs read fully. HEN protocol7/session works; all nine required privileged SVC hints0. Experiment correctly stops at CAPABILITIES, child_created0/game_started0; no actual kernel permission/creation test occurred. Not an OOT crash or allocation failure. Pool/thread values unchanged; no child/cleanup needed. Separate-process work requires a supported privileged broker/HEN path; do not bypass hint guard or overstate impossibility. User now testing normal resource-v3/host-end build; await its three logs. No code/binary change from this result.

## 2026-11-25 - OOT v3 logs: remote budget healthy at checkpoints, termination still uncaptured

Read entire gameplay(6)/crash(6)/host-end. v3 confirmed.21 successful remote samples: info31=192MiB, guest META+0x10=96MiB, info32 max24.375MiB through save_mounted38.036s. No observed remote shortage. Final management free65847296, threads248.729 memory returns succeed/0 fail, initial GPU path returns, no present. Host-end open succeeds but only512-byte header persists; no host exit/abort event captured. Absence does not prove kernel/GPU termination or rule out bypass/logger failure. Save FS mounts, individual save contents not validated. Full evidence/caveats appended to Findings.md. No new quota/execution changes or rebuild from these logs; missing post-mount terminal cause remains focus, not repeated identical tests. Process trial still separately blocked before creation. No push.

## 2026-11-25 - Actual post-save path found; profile mount tracing repaired

Firmware.23.0.1 source found on Desktop; packaged NHR0 NCAs/CNMT/archives, read-only inventory only, no extracted executable bodies or game startup sequence obtained. Following OOT executable produced a stronger lead: sole save MountSaveData call main+3b4fc10, expected return+3b4fc14, then helper+f92c90 and +eb87d0 leading into gmd::SaveDataMgr setup, allocations/light queue/TLS/CreateThread (+67ddf4). Static path, not proven runtime failure. No game-specific patch.

Our generic save mount recorded phases/caller but profile route did not: fixed that diagnostic gap universally. Caller propagated at all exported ABI entries, logged with profile flag in synchronous mount record; phase changes mapped to module/return/callsite offsets and flushed promptly. phase4 is bridge-ready-to-return, not proof of resumed game. Marker SAVE_BOUNDARY_TRACE_BUILD version1. Added bounded READ-ONLY exception-forwarder pointer audit (pattern-only, no variable/code writes or fault injection). Signed-ADRP/readability/read-only fixture and profile namespace tests pass. Full details appended to Findings.md. Native build/package pending at time of this entry; hardware test still required. No firmware/game/save changes or commit/push.

Save-boundary1 native build/package completed: all36 units compiled with -Werror, linked and converted successfully. Packaged NRO672016 bytes SHA-256 EF8E58FF601A1C06F26F7826409FA3D79D5E0995AF99F6FA5A696FF6AD658028. Raw and packaged NRO startup/segment validation passed; SAVE_BOUNDARY_TRACE_BUILD, EXCEPTION_FORWARDER_AUDIT and profile-return markers verified in binary. Prior resource-v3 build saved as release/previous/BackupLoaderMKW-resource3-before-save-boundary1.nro. Host read-only forwarder fixture and existing profile namespace test pass; console test pending. No claim that SaveDataMgr is proven fault site. Local only, no push.

## 2026-11-25 - Latest logs reviewed: actual save caller matches; bridge return unconfirmed

Full gameplay(7)/crash(7)/host-end(1) read. New boundary marker confirmed. Profile phase10 ->2, register result0, guest caller resolves main+3b4fc14 and callsite+3b4fc10 exactly matching static analysis. No persisted phase3/4 or profile gate-return text; can be missed during rapid termination, so no proof of stuck RPC or executed SaveDataMgr. Focus narrows to notification RPC completion/return-to-game boundary. Prelaunch exception-forwarder candidate contains correct NRO start0x8000000, readable, unchanged; no obvious stale/null pointer. Resources still have headroom; host-end header only, terminal cause uncaptured. Details appended to Findings.md. User preference: SOURCE ONLY, no compilation/packaging; user runs build.bat. No code/build/push this response.

## 2026-11-25 - SOURCE ONLY: nonblocking save-mount notice experiment (boundary v2)

Removed synchronous logging-only RPC_SAVE_MOUNT wait from both mount routes after real SDK register_fs already completes. Owned typed diagnostic queue now carries notification; game returns actual registration result regardless of diagnostic queue availability. Real profile/FS/data operations and errors unchanged. Late diagnostic flag no longer depends on host acknowledgment. New counters enqueued/served/dropped and notice queue-acceptance logs. Phase3 now enqueue-return (version1 was RPC-return); phase4 still bridge-about-to-return. Marker SAVE_BOUNDARY_TRACE_BUILD version2 mount_notice=nonblocking. This tests a concrete observer dependency at the last captured point, not a proven root cause or title patch.

Queue test source updated, static diff check passes. NO compilation/test binary execution/build/package performed per user instruction; existing release binary remains older and must be rebuilt with build.bat before testing. Hardware/compile validation pending. Normal A/GPU flag and same three logs; check boundary v2 marker. Full details appended to Findings.md. No push/commit.


## 2026-11-25 - Zelda OOT: early GPU completion, later successful save registration signal

Full latest203820 normal-A run with imported saves confirms boundary-v2. Observed acquire/submit/finish all return by about2.73s; loading continues to save_mounted37.981s. No frame attempt or terminal exception captured, host-end header only. This is not evidence of a pending traced GPU call at shutdown. Device initialization0/0 reflects incomplete observation: direct bootstrap resolution previously bypassed wrappers. Source now routes bootstrap and getproc through the same policy and guards cached self-wrapper targets; genuine calls/results retained.

save_mounted checkpoint requires successful SDK registration signal even though older phase10/notice0 lines persisted. Cannot say it remained in bind or entered later SaveDataMgr. New source flushes registration checkpoint before resource queries. Observed management/remote/thread budgets remain healthy; neither general-memory nor GPU-cause probability is established.

Added universal menu-Y system error recovery for the next source-built run. After OOT closes, reopen launcher and press Y before another launch; SD:/BackupLoaderMKW-system-error.log may provide error/addresses/registers from a matching host/time report. Requires new last-launch marker; cannot attribute old runs without it. Service/firmware format may be unavailable and this will be logged. Source only: no build or fixture execution; user builds via build.bat. Actual OOT launch/graphics fix still unproven.


## 2026-11-25 - Zelda OOT: system error recovered, device initialization succeeds

Latest gameplay(8)/crash(8)/host-end(2)/system-error/last-launch read completely. ERPT returns host0400c3f00006e000 error2168-0002 at marker+36 seconds. This reports a DataAbort fault class; exact instruction/address/component unknown, guest attribution remains host/time candidate. No whitelisted CPU/GPU-address fields in18500-byte parsed report; another8263-byte report rejected. Device init now1/1 success, demonstrating earlier0/0 was tracing coverage; queue7/7/window1/1 and no frames persist. This run does not capture successful save registration (gate0, no save checkpoint); earlier save_mounted observations belonged to other runs.

Y reader source v2 now logs matched report's bounded field names/types/encoded lengths, never unknown values; parser rejection field/tag/offset and plain CPU-context presence. Build/copy source, reopen launcher/Y before launching another game to reread this existing system report; no new OOT run required yet. No new root-cause fix or binary built. Docs append-only; no push.


## 2026-11-25 - OOT retained report has separate dump flag; Y v3 source prepared

Correct v2 upload received and read completely. Matching DataAbort report includes NxdmpIsAttached=true, no plain CPU fields, encryption metadata, no GPU-dump attachment flag. Does not identify fault instruction or prove CPU context can be recovered. Y v3 source now queries same-host report attachment metadata and crash-time resource limits/peaks, and distinguishes valid reports missing crash identity from malformed input. No attachment body reads or game behavior changes; user builds and rereads original report with Y, without rerunning OOT. Nothing compiled or packaged; docs append-only.


## 2026-11-25 - OOT new launch matched: DataAbort+37sec; system quotas below limits

Latest v3 system report fully read (Pasted text(2).txt156 lines). Host/time2168-0002 matches new marker1795656952 at1795656989; no fault addresses/CPU fields. Named System resource peaks all below limits, not verified per-guest allocation budgets. Nxdmp/syslog flags true; list request0x6693 means TooManyOutAttachments, not absent dump. Reader source v4 now grows list8/16/32/64 entries for that exact capacity error and validates all owner records before opens; no game execution fix or dump-body reads. User builds and presses Y, retaining current marker. No extra OOT run requested, nothing compiled/packaged; root cause remains unlocated.


## 2026-11-25 - OOT v4 host report: two attachments, owner-layout mismatch

Actual Pasted text(4)157 lines reviewed. New marker1795657989/fault1795658025,2168-0002, same below-limit System peaks and no plain CPU address fields. List succeeds with2, but reader rejects first owner; no attachment opened. Earliercapacityerror not proof of more than5 attachments. Public ID equality uses16 UUID bytes, our old20-byte check was stricter. Source v5 corrects it and validates direct/legacy-prefixed layouts, with diagnostic-only known UUID offsets for unknown formats. Nothing compiled/packaged; user rebuild/Y can reuse retained latest report. Actual fault address/root cause and attachment format not established.


## 2026-11-25 - OOT v5 actual upload: ownerless/unknown attachment response

Pasted text(5).txt158 lines confirmedversion5. Latest host/timeDataAbort+36sec; native list2 succeeds but ownerUUIDscan0, no plain CPU fields. Legacy ID correction insufficient. Modern ABI not established by public research. Source-only v6 tests strict compact64 metadata profile with real native Open/GetSize results and exact size validation, explicitly experimental, no bodies/exported IDs or fake replies. Unknown structures remain rejected. No fault address/root-cause fix established; nothing compiled/packaged. Reuse retained marker/report viaY after user rebuild; no new game test requested.


## 2026-11-25 - OOT retained report via v6: compact64 rejected

Actualversion6 upload confirmed/read fully. Native2 attachments but legacy layouts and compact64 candidate rejected;60 nonzero metadata bytes end96, exact format still unknown. No attachment opened/fault PC recovered. Sourcev7 now logs max256 bytes of attachment-list metadata to map actual wire format, not crash/syslog bodies. No game behavior change or build performed. Reuse retained report withY after user rebuild; no new OOT test requested.


## 2026-11-25 - OOT v7 identifies compressed diagnostics and60-byte native metadata

Actualversion7 upload confirmed/read fully. Latest marker1795659132/report1795659168 DataAbort; list wire reveals syslog.bin.lz4 26387 bytes and nxdmp.gz14725 bytes in60-byte records (sizeU32/name+28), no owner ID. Sourcev8 adds decoder/native exact-size validation and bounded export of these two matched diagnostic files with report-timestamp names. No native attachment/body success captured yet or fault PC located. User builds/Y with retained marker, sends version8 log plus compressed exports; no new OOT run requested. No compile/build performed, docs append-only.


## 2026-11-25 - OOT V8 diagnostic collection works on console

Native60-byte decoder and both exact-size body exports confirmed by latest full system-error log: syslog26387 bytes and nxdmp14725, all service/read/write/close results successful, complete1. Both uploaded compressed files present. Actual fault address not decoded yet because analysis-side binary download fails DNS; local Windows path requested. No new OOT test/source build needed. No game fix claimed or code changed; docs append-only.


## 2026-11-25 - OOT V8 actual binary analysis, early exception capture source

Both exact-sized diagnostic bodies read locally after user provided path. Syslog decodes to1597 MessagePack records; fault PC/registers not recovered. nxdmp.gz is not ordinary gzip/raw-deflate at checked wrappers; likely encrypted/private encoding but encryption not proven. DataAbort remains confirmed fault class, actual faulting component unknown. No new allocation/GPU/save root cause claimed.

Source now includes universal raw exception-entry observer before libounce touches original stack/GetInfo. Bounded100ms fault-only persistence opportunity, original handler always forwarded, first raw PC/LR/ESR/FAR/module offsets flushed by host. No game-specific patches/faked results or HBL pointer changes. Static checks only, not compiled/built. Requires a NEW OOT normal-A test after user build; seek RAW_EXCEPTION_CAPTURED in usual logs. No further Y-reader iteration requested. Full technical findings/handoff appended.


## 2026-11-25 - OOT raw-entry1 run: same DataAbort; raw native-sidecar source attempt

Current local logs fully reviewed: raw-entry1 marker present, ELF wrapper routing confirmed by disassembly, no persisted raw PC. New matched1795660890 hostDataAbort (+36sec), tiny syslog7 audio records/no PC, dump remains non-gzip. Does not prove exception entry absent or infer game root cause.

Source raw-entry2 attempts native512-byte binary capture directly from dedicated fault stack for DataAbort, preopened sidecar BackupLoaderMKW-raw-exception.bin, initial raw RAM observer preserved. Captured flag/checksum parser added and Python/static checks pass. Native fault IPC unvalidated/may fail or block, no OOT fix claimed. No build performed. User new build/test then bin+usual3 logs; Y stays8. Full caveats/findings/handoff appended.


## 2026-11-25 - OOT raw-entry2 trial: prepared sidecar only; SDK filesystem ABI checks match

Local launch1795661618 confirms raw-entry2 and native sidecar open result0.512-byte sidecar validates but captured=false, no fault PC/native exception/host-end event. Save profile binding returns0; last phase10 observation does not establish SDK registration completion or fault location. No matching current system-error report supplied in folder. Original SDK Register ownership convention and four inspected filesystem virtual offsets match bridge; no concrete ABI mismatch or new game fix found. No unchanged test repetition requested; no build/native test/commit/push. Full limits appended Findings/handoff.


## 2026-11-25 - Star Fox current-build working baseline acquired

Actual release/switch Star Fox marker1795665020 read and copied to ignored .work/starfox-1795665020. Gameplay3841/crash4219 lines; save-boundary2/NVNlookup2/raw2/resource3 markers. Same profile-bind43 succeeds then explicit SDK registration checkpoint phase4/enqueued1/served1. Continues62sec/2773 presented frames, resource samples through65.387sec, storage reads4061/4061. Yversion8 finds0 matching host crashes and2 reports missing crash identity, no new dump. Diagnostic crash filename does not mean Star Fox crashed.

Crucial limitation: working game itself transiently logs phase10 even with mount notice already enqueued. Do not call OOT phase10 a proven fault location. Latest OOT differs in no saved successful-register checkpoint, notice0 and no present; causality unresolved. RPC publication/completion memory ordering reviewed; no concrete new bug found. Full comparison in Findings. No new code/build/test request.


## 2026-11-26 - nx2-flow comparison identifies memory/startup/save implementation leads

Reviewed Olmectron/nx2-flow treef0861ffbeda36d73df6d59dcbac4b95281eea526, releasev0.2.1. No explicit named OOT success/host/build/log in reviewed docs; user report remains plausible but unverified. Same host limits, kaiHEN20261117.1 tested, no separate process escape. Both loaders return unused heap to kernel; flow uses roughly512MiB loader default versus our1024MiB and parses HeapDef BYML to distinguish reserved virtual space/report budget8992MiB. That reported figure is a heuristic; do not blindly copy it or shrink host backing. Flow reimplements rtld and starts SDK on original main thread, clearing user TLR120..1ff; ours originalrtld on libounce worker clears180..1ff (further SDK init remains to inspect). Flow save redirection is native SDK IPC/domain path rather than our custom C++ filesystem registration. Dynamic module merging is another verified difference, not proven OOT failure. Optional services are substituted by flow, so wholesale copying conflicts with user's preferences. Full evidence/limits appended Findings. No gameplay source patch/build/test/install/commit/push.


## 2026-11-26 - Source implementation: native main-thread SDK handoff, TLS, native save mount, HeapDef budget

User confirms nx2-flow OOT/MKW Discord video; explicitly requested adoption. Implemented default NATIVE_COMPAT_BUILD version1: prepare/monitor on host thread, SDK/original rtld on actual main kernel thread with NPDM-sized private stack and chosen permitted priority/core. Clears user TLR120..1ff, preserves kernel100..11f. Retains original game rtld, not replacement linker. SDK Mount*SaveData* exports left intact; native FSP domain backend resolves account/device saves to existing root/profile paths. Read-only respected, BCAT/cache/system separated, no saves moved. Native-open evidence counter/checkpoint distinct from SDK registration success; old C++ success marker absent is expected in new mode.

HeapDef reader supports folder/NUP union, bounded BYML/Zstd/SARC dictionaries, extracts virtual address reservation. Positive metadata activates logical budget bounded by real process entitlement/span+reserve and real measured program charge; allocations still kernel/ownership checked,64MiB headroom (fallback384MiB). Zero-size heap shrink accepted.1GiB host prefix retained due prior512MiB module backing failures; allocator not replaced. Full technical limitations in Findings.md.

Empty SD:/switch/BackupLoaderMKW/legacy-launch.flag restores previous worker/TLS/C++save/budget selection without rebuild; remove to use native default. Normal A first OOT test. Static Python C-grammar/wiring/guard checks and git diff --check pass, but no compiler/assembler/linker/NRO/native test/console install executed. Added adversarial heapdef C fixture source (UNRUN). MIT source attribution/license retained. Source experimental and not console-verified; no OOT success promised. Docs append-only; no commit/push.


## 2026-11-26 - OOT first gameplay succeeds then slow/frozen; performance source attempt

Run1795679052 actual desktop Loader Logs reviewed fully: native main SDK handoff succeeds, HeapDef2802MiB virtual reservation/8960MiB logical budget parsed, native private save opens. Presents4324then stop~120sec; loader/storage continue through132sec, no captured CPU exception. Observed6.4sec phase8 file-open wait for Tex/Rock_FairyFountain_Wall_D_Nrm.bntx.zs, maxI/O7113ms, queuedreads~6.4sec. User confirms transfers STOPPED. Physical/resource snapshots retain headroom,6948observedmemorysuccess/0failures. No proven GPU/CPU/memory root cause. Folder186587files versus nx2-flow NUP remains relevant performance difference.

Preserved logs/source/user-built NRO+ELF under ignored .work/oot-playable-1795679052; NROcf77a1fbfd7b6cf518f9838eaf0a90141a4212bd18f75ec56c1eedaf4061f760 unchanged. Source performance profile1: normal native A/L+A lighter counters/logs, deep GPU marker requires deep-gameplay-trace.flag/full-ipc-trace.flag or ZR+A. Error/rejection IPC only, buffered10sec routine flush/heartbeat,5sec boot/10sec gameplay crash snapshots. Fault/stop flush kept; routine tail can be lost on abrupt death. Ready filesystem RPCs served before diagnostic text, optional text drained on idle only (light1/deep4). Startup/TLS/native saves/memory/affinity remain as working run. Not a proven freeze fix; no build/native tests/install/push. Grammar/static/whitespace checks pass, details in Findings. User build.bat normal A same folder next.


## 2026-11-26 - Native library GUI and minimal diagnostics (source only)

Implemented probes/library-ui as a separate libounce frontend: 1280x720,
five columns / three visible rows / vertical scroll, rounded artwork tiles,
folder or game-card badge, two-line UTF-8 captions, real header library count,
SD free/total, connection status and battery. A is the sole game-launch control.
X filters Folder/NUP; Y sorts A-Z/Z-A/Folder First/NUP First; Minus Options
provides nine contrasting themes, Minimal/Verbose diagnostics, library refresh
and manual last-system-crash export; B closes menus/returns to hbmenu.
Settings persist in ui.cfg. Native original-main-thread startup, TLS, private
save routing, heap budgeting and working normal-A graphics path are retained.

Names and artwork are read from folder control files or NUP metadata; NUP raw
and Zstandard control file records provide an artwork fallback. The actual
local NUP samples lack ICON metadata, so no artwork is promised when a backup
omits control data. System font uses pl:u shared memory, libounce built-in
font fallback if denied. No proprietary font/artwork bundled or downloaded.
Display title is separate from Nx2Game.name, preserving save namespace.
Frontend also scans SD:/nups separately from SD:/games.

A writes a bounded/checksummed/one-shot launch-request.bin and requests
runtime/BackupLoaderMKW-core.nro through envSetNextLoad. All UI cache allocations,
framebuffer/VI2 layer, pl font mapping, psm/nifm sessions and catalog are released
before frontend main returns; hbloader replaces the frontend NRO. The GUI does
not intentionally retain its code/heap alongside the guest. The runtime still
needs the proven host reservation (not removed) and releases its own progress
console before guest entry. Both build paths/package scripts now stage both
NROs; copy the full release/switch tree, not only the public NRO. Handoff is
new/untested on hardware and must not be called a proven compatibility fix.

Minimal is now default and supersedes previous normal-A logging profiles:
small selected-title/host/preparation/failure log, fatal exception capture and
raw sidecar retained; no routine IPC event publication, ordinary guest log
queue/wakeup, GPU gameplay trace, periodic crash.log snapshots or heartbeat
text. Prior verbose crash.log is removed at minimal launch to avoid stale
report confusion. Verbose re-enables the detailed investigation path. Old
trace flags do not override GUI diagnostics. Legacy-launch.flag remains an
explicit source-era rollback override; no alternate GUI launch hotkeys remain.
Manual system report export is retained in Options rather than former Y.
No game-result faking, filesystem routing or permission behavior was removed.

Validation: Python C-grammar parsing of UI/media/request and modified runtime
entry/logging/handoff functions, source resource-release/5x3 geometry/wiring
checks, packaging-script syntax and whitespace checks. These do not constitute
SDK type/link validation. No compiler, assembler, native tests, NRO packaging,
console installation, commit or push executed. User runs build.bat. The preserved
playable OOT baseline in .work/oot-playable-1795679052 remains available; no
new OOT run or evidence resolving its slow/freeze behavior was provided here.


Final GUI source review: all nine primary text palettes exceed 4.5:1 contrast
against their background/panel/card colors (observed minimum 11.2:1); checked
local NUP package/file-table field bounds and metadata types. Preserved playable
OOT NRO SHA256 remains cf77a1fbfd7b6cf518f9838eaf0a90141a4212bd18f75ec56c1eedaf4061f760.
UI startup failure now writes a small BackupLoaderMKW-ui.log and offers a
return screen; these failure records remain even in Minimal mode. C grammar,
PowerShell/Python packager syntax and git whitespace checks pass. Bash syntax
checking was attempted with -n only, but MSYS startup was denied by this host
(NtCreateDirectoryObject 0xC0000022); no build script was executed. Neither SDK
compilation/linking nor hbloader/display handoff behavior is yet validated.


## 2026-11-26 - Reported GUI/art/title failures, large NUP rejection, resource gate

User reports unattractive blocky symbols, missing artwork, some NUPs named
META, NUP file-count rejection, a resource-refusal message (possibly OOT,
identity unconfirmed), ongoing Yoshi gameplay freezes and DK load freezes.
No new SD logs are available; desktop Loader Logs are still run1795679052.
Do not claim these gameplay failures are resolved by the following source work.

UI revision: Nintendo Extended font mapped through pl:u, actual filled/outline
A/B/X/Y/Minus glyphs selected when present, correctly centered bitmap bounds,
analytic antialias coverage for rounded geometry/logo/WiFi, less intrusive
missing-art placeholders. Expanded control-image filenames and read-only
nsGetApplicationControlData(CacheOnly) fallback. NS session closes with UI
resources; no NS download request, network probe, proprietary font/art asset
bundling or game-service stubbing. Backups without art and console cache may
still show placeholders. Current local NUPs have TLVs0/4/6/7, no ICON records;
local extracted Pokopia root also lacks a control directory.

META diagnosis: old title parser accepted the short META string at the start
of an NPDM as a valid NACP/title. New parser requires a complete language-table
span, rejects META/NPDM magic and generic labels, retains the filename when
metadata isn't a meaningful title. Actual affected SD archive not identified,
so the specific bad metadata tag is unconfirmed. Current local NUP title TLV4
values are KirbyAirRiders_GlobalTestRide and Pokopia, not META.

Large archives: removed fixed262144 caps in nup.c discovery/mounting and
rom_view.c; folder tree capacity now derives from 128MiB/sizeof(Entry), and
partial-cache record count derives from its actual byte span. Shared pure
nup_layout.h checks package/table bounds, checked multiply/add spans, child-ID
range, child alignment and pool extents before computing pointers. Existing
TOC SHA256, file extents, safe names, codecs and metadata budget remain. UI
embedded-art scan now follows the runtime128MiB TOC bound. No repack required.
Added test_nup_layout.c with legitimate300000-file and malformed/overflow
fixtures; it has not been natively compiled. A Python AST interpreter executed
the actual pure C helper and C fixture: all10 arithmetic boundary assertions
passed, without compiler/linker/native code. This is not full archive testing.

Resource comparison: preserved playable OOT run shows host67108864 bytes,
guest50331648 bytes. This same prelaunch check already existed before GUI.
Current Olmectron/nx2-flow loader/runtime/main.c logs the declared-system-pool
comparison but does not stop there. Our native path now treats a smaller
NONZERO configured pool as advisory, allows real SDK/kernel requests, and
passes existence1byte to the later VM release sanity check rather than
repeating a declared-full-reservation veto. Zero required pool and query
failure still refuse, legacy path keeps old gate. Real alias ownership,
physical-budget/kernel allocation/mapping checks are unchanged. No physical
memory or system-resource quota is invented. This may only remove a premature
startup veto, not fix a true exhausted pool or prove OOT restored.

Minimal logs keep RESOURCE_CHECK values/query result, explicit distinction
between failed query and small/absent pool, original SDK handoff, FIRST_PRESENT,
and up to4 compact event-triggered suspected-render-stall capsules (IPC tail,
active storage operation/path, no constant verbose stream). A pause alone is
not a proven deadlock. Launch request v2 additionally checksums frontend host,
pool/query result and PID, enabling a before/after comparison across hbloader.
An incompatible request now shows a clear copy-both-NROs message rather than
silently reopening the GUI. Catalog game identity/save paths remain unchanged.

Validation: C grammar checks for revised GUI/media/request/runtime entry,
logging, main handoff, complete gameplay loop, NUP reader/layout helper and
RomFS reader; resource cleanup/wiring and whitespace checks. The large-file
arithmetic helper was interpreted as above. No SDK compiler, assembler,
linker, native tests, packaging, console test, commit or push. The preserved
playable OOT baseline remains untouched. Further Yoshi/DK/startup root causes
need fresh run evidence; do not advertise every title fixed or source parity
with the other launcher's full loader/allocator implementation.


## 2026-11-26 - Read-only three-repository architecture comparison

Completed an initial source-only architectural comparison against pinned
nx2-flow9ad10e2bebb0cf02082c413c06b0dcae25926014 and nx2-relink
b3e0fc4540784d45cd40dd9a97b1f4a5d0bacaad. Report:
docs/runtime-architecture-review-2026-11-26.md; hashed input manifest alongside.
87 local source files frozen under .work/architecture-review-20261126/
local-before-followup;58 upstream text sources retained with license files.
No runtime source edited during the comparison itself. No build/packaging/
installation/native tests/commit/push/new dependency installation performed.

Confirmed differences: original rtld versus custom ModuleObjects/SDK start;
fixed allocator prefix versus separately owned large arena/tail trimming;
our single NUP RomFS root versus reference ordered base/update overlay;
newest-plus-base folder handling versus all update layers; RO session rollover
versus TLS-aware module merge planning; explicit MKW-only gate contradicts
host-agnostic intent; legacy NUP TLV namespace handling; root-mtime cache
freshness blind spot; GUI dropped explicit index rebuild action; minimal host
logging loses SDK_BREAK/SDK_ABORT. Last four small corrections can proceed
without replacing startup architecture. Larger items require isolated changes,
fixtures, measured ownership and hardware baselines, not a universal-fix claim.

Flow establishes on-console NUP preparation with a generic loader and no
per-game PC build, but documents remaining module/TLS limits. Relink currently
uses PC per-title preparation; applicable runtime strategies can inform a
console implementation without adopting that workflow. Full reference parity
is not established: references include optional-service success stand-ins,
visible-memory constants and protection suppression that must not be imported
blindly under the user's real-error/standards requirements. Applet/HID ownership
handoff is a specific review concern, not yet a proven game freeze cause.

User's requested regression matrix and rollback points are in the report.
Only preserved old OOT log is available:64MiB host pool/48MiB declaration,
4324presents before stop. Recent resource-refused title remains unconfirmed;
no fresh GUI/game logs yet. Star Fox/Fast Fusion working baselines retained;
Yoshi/DK/Drag x Drive freezes and Kirby/Minecraft startup problems remain open.


## 2026-11-26 - Small corrections after the frozen architecture review

Implemented source-only: remove hardcoded MKW host-ID eligibility while keeping
real capability checks; preserve and flush BREAK/ABORT/FAULT/STOP diagnostics
even in Minimal and after routine-log quota; normalize known0/1-based NUP fact
namespaces using a validated NPDM/ACI0 anchor (UI and NPDM fallback); reject
nonimage bytes before they block valid artwork fallback. A valid name beginning
METAL is not a binary META header. Added nup_meta.h and adversarial C fixture
source, not natively compiled. No service stubs or fake kernel success added.

Restored on-console selected folder-index rebuild in Options: only generated
.idx and selected .part checkpoints removed through checked cache functions;
errors shown, no immediate game launch, next normal A rebuilds using existing
resumable scanner. NUP tables and all saves/game files preserved. Shared index
code linked into frontend from existing project units, no new dependencies.

These follow the read-only input snapshot/report; updated report labels the
larger update-union, module-merge/linker and separate-arena work UNIMPLEMENTED.
No applet/HID ownership behavior, original-rtld sequence, thread/TLS behavior or
save migration was changed here. No title's freeze/startup problem is declared
fixed. No compile/assembler/linker/native test/NRO packaging/hardware install/
commit/push was performed. C grammar/source wiring/cleanup/whitespace checks
and prior10 interpreted bounds assertions are limited source validation only.
The retained OOT binary remains the rollback point, not proof for new source.


## 2026-11-26 — Follow-up 1795712522: actual resource gates, profile consistency, storage stall

Source-only follow-up documented in [docs/runtime-followup-1795712522.md](docs/runtime-followup-1795712522.md). The current release/switch logs were preserved before changes in `.work/runtime-followup-1795712522/`, with a source-before rollback snapshot and change manifest. Available reports are Star Fox folder reaching first present and OOT NUP reaching 1704 frames before an 8-second pause with a ROM read still active and zero completed storage requests since progress. This does not prove a GPU deadlock. Raw record has captured=false; ERPT matched zero reports. No fresh failing Yoshi/DK/Xenoblade/ZA/Mario Tennis logs remain in this bundle.

Native NPDM system-resource declaration checks are advisory, including zero host pools, in both preparation gates. Actual kernel totals/used/free-known are reported, and real heap/mapping failures still propagate; this cannot increase Pokopia's actual kernel quota. Generic allocator/range/headroom policies remain and are explicitly enumerated in the detailed follow-up. Minimal diagnostics now retain sampled local refusal and real allocation-failure labels rather than silently losing them.

UID-less device mounts and archive ensure/create helpers now share the real HBL selected UID (or first explicit SDK UID) profile route. Existing roots/primary bindings/save metadata remain in place, with no Yoshi-specific exception or migration. Missing UID falls back to the existing legacy behavior and is reported; validate on actual HBL. Nonexistent secondary-profile archive is an absent existence result; genuine FS errors propagate. This is not yet a verified Yoshi fix.

Folder LRU 16->64; optional validated NUP directory binary search plus exact-path lookup cache; bounded raw read ceiling 64 KiB->1 MiB. Native handoff releases the menu HID session. Freeze capsules now resolve ROM indexes and include read/decode progress. Previous root reports are archived during preparation into 16 bounded previous-* slots (8 MiB/file); full/failing archive reports -1 and never blocks launch. Snapshot files may identify different runs. No per-read successful disk logging was added.

Grammar/source-wiring and whitespace checks only; no compiler, assembler, linker, build, packaging or native test executable was used. User must build and test Star Fox/Fast Fusion regression first, then Yoshi/OOT/Mario Tennis; folder/NUP independently. User-reported Xenoblade/DK hard crashes, ZA/Mario startup failures and Yoshi/OOT freezes remain unresolved hardware outcomes, not fixed-title claims. Source revision label: 20261126_storage_profile_pool_1.


## 2026-11-26 — PLZA run 1795713343: SDK driver-service connection assertion identified

Read-only diagnosis: [docs/plza-driver-startup-1795713343.md](docs/plza-driver-startup-1795713343.md). The PLZA-host report has 16 MiB actual and declared bookkeeping pools, successful module mappings/SDK handoff and 8434581504 bytes available after preparation. It aborts before first present. The user's PC NUP was read at `D:/Roms/Switch 2/Pokemon Legends Z-A.nup`; archive entry hashes were checked, SDK disassembled read-only, and the exact eight reported break-site instructions matched.

Confirmed assertion: CreateNvDrvByHipc, `result.IsSuccess()`, nvos_drv_DriverImplByHipc.cpp line 48. Its failing operation is SDK ConnectToHipcService for `nvdrv`. The numeric failed Result was not retained; the zero generic SDK-break payload is not that Result. NUP base/update NPDMs are 0100f43008c44000 with an older SDK; host is 0400f43008c44001. No alternate native ExeFS exists in its content-only package. PLZA SDK has no GSV exports; working Star Fox SDK has 61 GSV-related function exports. Libounce's actual Switch 2 NV initialization goes through GSV, not the old SM/transfer-memory initialization sequence. nx2-flow's older-NVN backend explicitly addresses this architecture gap, with limited API/shader coverage. Do not assume a same-product host resolves a different-platform graphics backend or that a blind service-handle swap is compatible.

Universal diagnostics added, not a playability fix: optional symbol/ABI-based SDK service connection observer forwards the real Result; fatal SDK break drains pending failure diagnostics and prints 32 existing IPC breadcrumbs. No title-specific ID/offset/hash patches, aliases, fake responses, extra dependencies or GPU backend substitution. Revision is now 20261126_storage_profile_pool_2. Supplied PLZA log predates the prior and current follow-ups. Source-before snapshots and change hashes are in `.work/plza-1795713343/`.

Exact failure Result, workable legacy graphics compatibility and PLZA playability remain unresolved. No common root cause for other games is claimed. Grammar/source-wiring and whitespace checks only; no build, link, packaging or native test executable was run. No hardware success claimed. Keep the earlier source-only fixes and build/copy workflow; inspect SDK_SERVICE_CONNECT_FAILED / SDK_BREAK_PENDING / SDK_BREAK_IPC in the next applicable run.


## 2026-11-26 — Six-title batch reviewed; compatibility integration still incomplete

Reviewed all 24 supplied files in `release/switch/Logs`; all six primary reports are revision 20261126_storage_profile_pool_2. See [docs/compatibility-batch-20261126.md](docs/compatibility-batch-20261126.md) and its input hash manifest. Yoshi aborts on our own 0x2f5e02 from virtual FSP command 51; this confirms a loader save-bridge rejection, without identifying the exact validation branch. PLZA's nvdrv connection returns 0x1015. OOT (650 presents) and Bananza (5100 presents, first frame 63.549 s) have storage_slot=0 at the captured eight-second freezes; do not reuse older active-storage-stall conclusions for these runs. Xenoblade records a real 1 MiB MapPhysicalMemory failure 0x10801 but subsequently presents. Mario Tennis has no captured terminal cause. All raw records have captured=false.

No runtime code changes, new diagnostics, builds, installs or pushes in this review. Prior partial ports are not full runtime parity: ordered update unions, memory-arena ownership and merge/linker/initialization integration remain incomplete. No fundamental blocker to component reuse identified; original-rtld vs custom-linker ownership, private-save/profile adaptation and separate legacy graphics backend are concrete integration work. Using the working reference for immediate gameplay is reasonable; no claim that changing models/subscriptions guarantees a technical solution. Further development should focus on coherent runtime integration rather than accumulating more tracing. No game-specific validation bypass or hardware success claimed.


## 2026-11-26 — Functional adaptation: ordered NUP assets, private-save identity, theme input

Implemented source attempts, revision 20261126_ordered_nup_save_3; detailed evidence/limits: [docs/ordered-nup-profile-adapter-20261126.md](docs/ordered-nup-profile-adapter-20261126.md). Reference flow uses a shared per-game save directory or explicitly configured named slot, not automatic account-UID namespaces; reviewed relink uses its per-port save directory. Retained our existing real-UID primary binding/secondary profile paths and metadata. No migration/deletion.

NUP now separates newest program selection from ordered base/update asset layers, excludes unrelated apps/DLC, accepts a program whose assets are inherited, and resolves/enumerates winning paths consistently with directory merges/file barriers. This adapts the reference's preparation behavior without replacing folder support or the execution runtime. Existing direct read/checksum/codec bounds and reusable caches remain. Temporary union metadata is freed; no complete game data is copied.

Native account/device save requests can now carry the real host application identity and still route exclusively to the selected guest's private SD namespace. Zero/current and explicit guest remain accepted, foreign IDs rejected, real UID routing retained. Yoshi's known 0x2f5e02 command-51 abort motivates this attempt, but the exact rejected input was not captured and no Yoshi success is claimed. A on Options Theme is ignored; only left/right changes color.

Production C AST interpretation passed 12 selection + 5 save identity assertions. Grammar/wiring/whitespace checks passed, including added overlay fixture and updated 1 MiB native-read/profile fixtures; no native fixture execution, SDK compilation, link, build or packaging. Actual archive model: PLZA two layers restores one path; Mario Tennis two layers and Yoshi one layer add zero paths. Therefore do not attribute their startup/save failures to missing asset paths on this evidence. Full reference linker/arena/service/graphics integration and freezes remain unresolved. No extra diagnostic layer or title-specific runtime patch added. Rollback snapshot/hash manifest: `.work/compat-integration-20261126/`.
