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
