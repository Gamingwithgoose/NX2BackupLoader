# NX2BackupLoader

A folder browser and experimental native launcher for decrypted ExeFS/RomFS
backups under `/games`, using **Mario Kart World's homebrew title override**.
The console continues to identify the host as MKW.

## Current release: 0.13

Merge `release/switch` into the SD root, fully close MKW, then open a fresh MKW
homebrew override and NX2BackupLoader. Select a game with Up/Down or the left
stick and press **A**. **X** refreshes the list; **PLUS** exits while browsing.
The old diagnostic menus and unconditional three-minute gameplay timer are gone.

Both `/games/Name/{exefs,romfs}` and `/games/Name/base/{exefs,romfs}` are discovered.
The selected folder supplies its own modules and directory index. No per-title
whitelist, executable hash profile, fixed game-code offsets or separately prepared
RomFS map selects what can be attempted. Updates/containers are not merged or
installed; selections use the extracted root/base directories.

## How it launches

The loader discovers rtld/main/subsdk*/sdk, resolves SDK exports and validates
syscall wrapper instructions before placing detours. Original rtld handles game
relocation and startup. It releases the unused homebrew heap tail so the original
SDK can use real kernel virtual/physical memory allocation. A bounded file cache
backs a metadata-only RomFS view with loose SD assets. Saves stay on SD in separate
program-ID directories; the original demo's existing save directory is preserved.
No tickets, signatures, installed titles, ownership databases or HEN files change.

## Evidence and compatibility

The user confirmed Yoshi reached menus and gameplay in 0.12. Its report records
ongoing rendering through 175 seconds; the armed 180-second diagnostic timer
accounts for the closure. That timer is removed from 0.13.

The linked generic resolver prepares Yoshi, Zelda OOT, Mario Kart World and Kirby
Air Riders fixtures, resolves their 20 bridge interfaces and 68 hooks, and rejects
a damaged wrapper. The Yoshi hook sites match the hardware-working legacy set.
The runtime index matches Yoshi's prior index byte-for-byte and validates Zelda's
186587 files. Twelve linked-code emulator checks and 28695 host C assertions pass.
See `probes/game-launch/tests/pc-validation.txt` for exact binary hashes and scope.

These are PC preparation/regression checks. The 0.13 browser and general launches
still need hardware testing, and other titles have not been confirmed playable.
SDK ABI changes, unavailable services/resources, legacy heap allocation and other
runtime differences may require broader compatibility work. The loader refuses
unrecognized wrappers/interfaces rather than applying another game's offsets.
This is a general launch path, not a guarantee that every title works.

The current limits include 13 modules, 262144 files/directories, a 128 MiB index,
64 folder levels, 512 MiB mapped per module and MKW's inherited capabilities.
Large SD folders show indexing progress and may take several minutes. There is
no timed gameplay exit; the remaining five-minute startup limit stops only a game
that has not entered its instrumented main function. Use normal HOME/Close Software
when finished. Copy `/nx2-launch-report.txt` before the next launch when reporting
an issue. See `release/README.txt` for layouts, save locations and details.

## Build

Use `build.bat` with ounce-devkit/libounce. The root build targets
`probes/game-launch`; it verifies the Switch 2 NRO record and publishes the merge
folder under `release/switch`. Metadata-only legacy fixtures and earlier test
suites remain as development evidence; Nintendo binaries are not distributed.
