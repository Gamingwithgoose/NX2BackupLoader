# BackupLoaderMKW

> **USE AT OWN RISK.** I am not responsible for bricks that may or may not occur as this is still in early development and experimental. Back up your SD card and saves first.

BackupLoaderMKW is an experimental **Switch 2** homebrew launcher for extracted backups and, on this branch, NUP archives. It uses **Mario Kart World's title override** because Mario Kart World provides more memory for homebrew. The console still sees MKW as the host.

**Yoshi and the Mysterious Book Demo** has reached gameplay from an extracted folder and the title/profile-selection screen from a fast NUP. Other games remain experimental and may crash or stall. No games are included.

## What you need

- A Switch 2 running compatible kaiHEN/Homebrew Menu software.
- Mario Kart World and its **title override** for launching Homebrew Menu (not Album mode).
- Your own extracted game files and an SD card.

**Experimental branch:** NUP v1 base-game archives now have a direct-launch path. The app reads ExeFS modules and RomFS assets from the archive, decompressing Zstandard entries when needed. This has been tested on Switch 2 hardware, but compatibility varies by game; some archives or games still fail. Encrypted containers are unsupported.

## Put games on the SD card

Create a folder called **games** at the root of the SD card. Extracted games can use **exefs** and **romfs** folders as before. Experimental NUP v1 base-game files can be placed directly in that same games folder. You can mix folders and NUP files.

    SD:/
      games/
        Yoshi and the Mysterious Book Demo/
          exefs/
            main
            rtld
            sdk
            main.npdm
            (any subsdk files supplied with the game)
          romfs/
            (game files)
      switch/
        BackupLoaderMKW/
          BackupLoaderMKW.nro

Also accepted: SD:/games/Game Name/base/exefs and SD:/games/Game Name/base/romfs. Updates are not merged into the base game automatically.

## Running the loader

1. Copy the **contents** of release/switch to the SD card root.
2. Fully close Mario Kart World.
3. Start Homebrew Menu using **Mario Kart World's title override**.
4. Start BackupLoaderMKW.
5. Select an extracted game folder or NUP archive using Up/Down or the left stick. Press **A** to launch, **X** to refresh the list, or **+** to exit. Hold **B** while pressing **A** to rebuild an extracted game’s index. While a large folder is being indexed, press **B** to cancel and return to the game list. Cancelled scans do not yet resume.
6. Use HOME / Close Software to stop playing.

If the list is empty, check that extracted games have valid exefs and romfs folders or that NUP archives are complete and valid. Extracted games use batched directory reads where supported and can save progress to a .part file in SD:/switch/BackupLoaderMKW/cache/. Press B during scanning to stop; launching the same game again resumes after completed directories instead of starting over. A directory interrupted halfway through is rescanned. After a full scan, the loader saves a compact .idx file for later launches. Hold B+A to force a rebuild after changing game files. The cache only checks the top-level romfs timestamp, so nested file changes can go unnoticed. NUP games read their existing archive table; they do not scan extracted directories or unpack the entire archive. A file with a .nup extension is listed even if its initial validation fails, with the reason shown when selected. Incomplete transfers and unsupported archives are not launchable.

## Where saves go

Each game now gets its own folder:

    SD:/switch/BackupLoaderMKW/(game folder name)/

The folder name is the same one shown under SD:/games. Renaming the game folder changes its save location.

**Coming from NX2BackupLoader?** Old saves are still under SD:/switch/nx2-backup-loader/saves/. This update does **not** move or delete them automatically. Back them up, then copy the old save folder's contents into the new game-named folder. The previously working Yoshi demo used the old **yoshi-demo** save folder. Keep your old saves until you've checked the new copy.

## Gameplay / crash log

Launch logs depend on the selected format:

    SD:/BackupLoaderMKW-gameplay.log  (extracted game)
    SD:/BackupLoaderMKW-nup.log       (NUP archive)
    SD:/BackupLoaderMKW-crash.log     (separate crash diagnostic)

Copy relevant logs **before launching another game**, because the next matching launch overwrites them. The separate crash diagnostic records periodic host state and, if captured, an application-local CPU exception. An abrupt system-initiated termination may leave only the last flushed snapshot; it is **not** a kernel or GPU crash report.

For the complete project status, compatibility evidence, limitations, universal-fix roadmap and next-day handoff see [Findings.md](Findings.md) and [HANDOFF_2026-11-21.md](HANDOFF_2026-11-21.md).

## How it works

The loader discovers the selected backup's executable modules and files. It connects supported game SDK calls, lets the game's original startup code run, and handles supported game file and save requests through the SD card. Mario Kart World remains the system's host title, so the loader has MKW's memory allocation and permissions — not necessarily every resource other games require.

It does not install game tickets, rewrite game ownership, or install backups to NAND. The extracted-folder launcher reads loose files from SD. The experimental NUP path reads game files from the archive instead. Different titles have different services, graphics, and memory needs, so there is no promise that they will all launch or save correctly.

## Quick walkthrough: build, package, launch

**Build the launcher (NRO):** Clone or download the repository, follow the Windows SDK prerequisites below, and run `build.bat`. Copy the **contents** of `release/switch/` to the SD card root.

**Prepare games:** You can copy a game with `exefs/` and `romfs/` directly under `SD:/games/Game Name/` (or under its `base/` directory). Alternatively, for a NUP, place each extracted game folder under `extracted/` beside the BAT files, then run `build-nup.bat`. It writes one `nups/Game Name.nup` per input without changing the inputs or overwriting existing archives. Copy selected output NUPs into `SD:/games/` alongside any extracted folders.

**Run:** Close Mario Kart World, open Homebrew Menu using its title override, launch BackupLoaderMKW, select a game, and press A. If it fails, inspect the applicable overwritten log file before launching anything else. Current NUP support is experimental; packing a fast NUP helps with large random-access reads but does not guarantee a game will boot.

**First-time NUP packaging requirements:** Windows Python 3 (with `py`), Git, and internet access. The BAT installs the upstream Python NUP packer from GitHub plus `zstandard` into the user's Python environment when missing. It does **not** depend on any personal NUP-Tool checkout. The NRO build has separate devkitPro/ounce-devkit requirements described below.

## Building yourself on Windows

Download the repository as a ZIP or clone it with GitHub Desktop. Keep the directories together. Save the provided **BackupLoaderMKW-homebrew-icon.jpg** from the project chat as **assets/BackupLoaderMKW.jpg** inside the repository (create the assets folder if needed), then double-click **build.bat**. The build embeds that JPEG into the NRO's Homebrew Menu assets. If the file is missing, the build reports an error instead of silently producing an iconless release.

**Tools used:**
- **devkitPro + MSYS2:** Install it first from https://devkitpro.org/wiki/Getting_Started. This provides the Windows build shell.
- **ounce-devkit/libounce:** The Switch 2 SDK at https://github.com/ouncebrew/ounce-devkit. If missing, build.bat downloads its official installer into the **tools** folder next to build.bat and runs it.
- **PowerShell:** Included with Windows. Used to fetch the SDK helper and verify the output.
- **Git or GitHub Desktop (optional):** Helpful for updates.

Temporary setup tools stay in the repository's local **tools** directory for portability. The full devkitPro/MSYS2 compiler lives in its normal installation location, not alongside the BAT file. A missing Switch 2 SDK stops the build; it will not make a Switch 1 binary by mistake.

Successful output: **release/switch/BackupLoaderMKW/BackupLoaderMKW.nro**. Copy the contents of release/switch to the SD card root. The build verifies the Switch 2 executable format but cannot test actual gameplay.

## Current limitations

**Yoshi and the Mysterious Book Demo** has reached gameplay in extracted-folder form and title/profile selection using a fast NUP. Other games remain unverified or have known startup failures. It selects a base package, or a standalone-update package explicitly marked with the NUP v1 standalone flag; ordinary updates and DLC are not merged. Large compressed files may load slowly because they must be decompressed from the beginning when accessed at random offsets. Some games require services or memory that MKW does not have. The startup timeout is five minutes before game main entry, but there is no gameplay timer.

**Early experimental release. Use at your own risk.**

## Experimental NUP and index test

Use the experimental-nup-support branch, run build.bat, and keep the old working NRO as a backup. Test an extracted game first: start indexing, press B after a few progress updates, then select it again. You should see the previous completed directories recovered and the file count jump forward. After a successful scan, the next launch should load the saved index. Then select a NUP file. The selected entry shows either its validation status or a reason it cannot be launched. Press A only if the entry is valid. It should attempt the same native launch path without extracting the archive. The SD root gameplay log records NUP_OPEN_OK, NUP_ROMFS_INDEX_BUILT, module preparation and any launch failure. Copy the log before another test replaces it.

Direct NUP support reuses the Zstandard decoder already bundled with this project (see vendor/NOTICE and vendor/COPYING). No games or Nintendo executables are included.

### Known OOT launch blocker

The current extracted OOT test successfully built/loaded a cached index for 186,587 files and prepared all ExeFS modules. Its startup previously stopped at `Refused legacy SetHeapSize while native virtual memory is active` during `NNINIT_STARTUP`. The experimental native-VM heap hook now calls the real kernel `svcSetHeapSize` only when the request fits the original host heap region, preserves the existing host allocator and leaves at least 3 GiB of physical budget for other native allocations. A kernel refusal is returned to the game and logged as `NATIVE_HEAP_SET_RESULT`; requests outside the bounds are logged as `NATIVE_HEAP_BOUNDS_REFUSED` or `NATIVE_HEAP_BUDGET_REFUSED`. This is untested on real hardware and does not guarantee OOT can allocate everything it needs.

### NUP package support

NUP v1 archives can contain a base package or an update explicitly flagged `standalone_update` (flag bit 0). Both are eligible for experimental launch when their ExeFS and RomFS trees exist. Update-only archives lacking that standalone flag are not launchable without a base, and DLC/update layering is not yet implemented. If validation fails, the selected entry shows package index, kind, flags and directory indices to assist diagnosis.

### Separate NUP launch logs and native heap diagnostics

Selecting a NUP now writes `SD:/BackupLoaderMKW-nup.log` instead of overwriting the extracted-game log `SD:/BackupLoaderMKW-gameplay.log`. A NUP that returns to the menu should show an error on the menu; the dedicated log contains the selected file and the failing preparation step. The NUP mount reader now reports TOC, directory, or file-extent validation errors instead of returning silently.

For extracted OOT, the kernel heap request still uses the real `svcSetHeapSize`, with a 3 GiB physical-budget floor. The checker now validates the **retained heap's end** rather than assuming a `QueryMemory` result at the heap base represents one contiguous region. Logs include `NATIVE_HEAP_BOUNDS_INPUT`, `NATIVE_HEAP_TAIL_INPUT`, and either `NATIVE_HEAP_SET_RESULT` or a refusal. These paths remain hardware-untested; preserve the previous working NRO.

### November NUP/OOT startup test

The NUP tester reached `NUP_NPDM_FAILED` before native startup. The experimental NUP reader now tries the original ExeFS `main.npdm` and, if that fails, the archive's package metadata TLV type 0 NPDM. `NUP_NPDM_ENTRY` and `NUP_NPDM_METADATA_FALLBACK` record which source succeeded. NUP archive mount also avoids the former duplicate TOC read and checksum pass.

Extracted OOT reached `GAME_NNMAIN_ENTERED`, mounted RomFS and read `System/HeapSize/Product.Nin_Ounce_NVN2.HeapDef.byml.zs`, `System/RegionLangMask.txt`, and `System/GameRomConfig/NormalProduct.game__RomConfig.bgyml` before an SDK `Break` (reason 0). This is a deliberate game/SDK break, **not evidence of slow SD loading**. The break hook now records `caller` and attempts to identify the mapped code region via `SDK_BREAK_CALLER_REGION`. Do not bypass that break without resolving the originating assert/condition. Changes remain untested on hardware.

### Latest OOT/NUP diagnostics

The NUP run reaches `NUP_NPDM_ENTRY present=1 bytes=1828 codec=1`, confirming its NPDM file exists but is Zstandard compressed. The metadata TLV fallback also failed. This build does **not** yet establish whether the failed entry was unreadable, had a Zstandard decode error, or failed its SHA-256 check. The next log includes `NUP_NPDM_READ reason=...` and the stored extent information to distinguish these cases. No integrity check was bypassed.

Extracted OOT successfully enters `nnMain`, mounts RomFS, and reads three startup files before `SDK_BREAK` from SDK code region offset `0xBC03C`. It did **not** demonstrate slow loading at that stage. The next run captures eight instruction words around the caller using `SDK_BREAK_CODE` so the source of this break can be analyzed; the handler still stops safely instead of suppressing the break.

### NUP compressed NPDM decoder fix (experimental)

The 1,259-byte NPDM frame extracted from OOT's NUP independently decompresses with standard libzstd to 1,828 bytes beginning with `META`. The previous bundled decoder used `ZSTD_ZBIC_SUPPORT=1`, which selected Nintendo ZBIC frame magic and its altered FSE normalized-count decoder even for ordinary NUP Zstandard frames. The shared vendor decoder now selects standard Zstandard magic/FSE when reading NUP entries, and `nx2_zbic_decode` temporarily selects ZBIC for original NSO segments. This avoids including a second copy of the giant Zstandard translation unit and keeps both formats available. Neither a full Switch 2 hardware build nor runtime success has been verified; avoid assuming the NUP launches before testing.

Game startup speed after `nnMain` is unchanged by this fix. The extracted OOT startup break requires its own investigation. Yoshi first-frame startup time is a separate useful baseline.

### Shared OOT folder/NUP SDK startup break experiment

Both OOT sources now pass NPDM/NSO loading, construct the 186,587-entry RomFS index, successfully request a native heap, enter `nnMain`, mount RomFS, and read the same three startup files. Both then stop on `svcBreak` from the same SDK break-wrapper address (region offset `0xBC03C`), with diagnostic value zero. NUP packaging is no longer the differentiating failure. This is a guest-startup compatibility problem, not evidence of an SD read stall.

The native guest `GetInfo(InfoType_ProgramId, CUR_PROCESS_HANDLE, 0)` now returns the **selected game's** title ID, rather than Mario Kart World's host title ID. Only this one information type is virtualized for hooked game SVC requests; real kernel memory queries, physical-memory allocation and host process identity are unchanged. `GUEST_PROGRAM_ID` records each intercepted guest request. This is a plausible compatibility correction, **not yet verified as the cause of OOT's assertion**.

If the SDK still breaks, `SDK_BREAK_STACK` and `SDK_BREAK_STACK_CODE` log bounded, mapped, executable-looking return addresses from the guest stack to locate the actual call chain. The handler remains fail-closed: no break suppression and no arbitrary retry. Both folder and NUP use the same code path. Hardware build/runtime testing remains outstanding.

### NUP v1 random-access performance (Yoshi demo)

The Yoshi NUP now passes `PREPARE subsdk0` and enters `nnMain`, confirming the large-module decompression correction worked. The captured run has `frames=0` at six seconds, while the extracted folder reaches graphics presentation and performs reads far into multi-gigabyte Unreal Engine .ucas assets.

NUP v1 compresses each file as one Zstandard frame, with no per-file random-access seek table. Reading near the end of a compressed multi-gigabyte .ucas file forces the current native reader to decompress from its beginning on every read. This is a performance limitation of the container/read path, not an OOT-specific failure.

The `tools/pack_nup_fast.py` wrapper uses the existing upstream NUP packer and its RAW fallback to store files larger than 16 MiB uncompressed. Smaller assets remain compressible. The resulting file remains standard NUP v1 and preserves direct offset-based access. This can increase archive size.

Install the upstream `nup` Python package and `zstandard`, then run from the project checkout:

```powershell
py tools/pack_nup_fast.py YoshiAndTheMysteriousBook_Demo Yoshi-fast.nup --name "Yoshi and the Mysterious Book Demo"
py -m nup.cli verify --fast Yoshi-fast.nup
```

Keep the previous NUP until the replacement archive is verified. This does not fix the separate SDK break observed in both OOT formats. Log files retain their existing overwrite-on-launch behavior.

## Batch NUP Builder (Windows)

The project root contains two separate build scripts:

- `build.bat` builds the Switch 2 launcher NRO.
- `build-nup.bat` builds NUP archives for every immediate game folder in `extracted/`, placing each archive into `nups/`. This does not build or modify the launcher.

Example layout:

```text
NX2BackupLoader/
  build.bat
  build-nup.bat
  extracted/
    Zelda OOT/
      exefs/
      romfs/
    YoshiAndTheMysteriousBook_Demo/
      base/
        exefs/
        romfs/
  nups/
    Zelda OOT.nup
    YoshiAndTheMysteriousBook_Demo.nup
```

Double-click `build-nup.bat`. It needs Python 3 with the Windows `py` launcher. On the first run it installs the upstream `a55uka/nup` (`meow` branch) Python packer and the `zstandard` package if missing. Git and an internet connection are needed for that first-time installation. Subsequent builds use the installed packages.

This batch workflow uses the existing `tools/pack_nup_fast.py` optimization: files larger than 16 MiB are stored in the standard NUP v1 raw codec so the loader can seek efficiently, while smaller files are Zstd-compressed where useful. The produced filenames have no `-fast` suffix. Generated NUPs may be larger than fully compressed equivalents.

For safety the builder **does not overwrite** existing archives. To rebuild one, move or delete only its corresponding file in `nups/` and rerun the batch. The script performs a fast NUP structural verification after each pack. It does not delete or change extracted game input folders. Both input and output directories are ignored by Git.

Copy desired archives from `nups/` to the SD card's `/games/` directory. This is a packaging/performance workaround for the current loader; loader-side random-access caching for existing compressed NUPs is still planned. It does not resolve OOT's SDK break shared between its extracted folder and existing NUP.
