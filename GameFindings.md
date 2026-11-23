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
