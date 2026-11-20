NX2BackupLoader Folder Launcher 0.13

Merge release/switch into the SD root, replacing the existing app.
Fully close MKW, start its homebrew title override, then open NX2BackupLoader.
The first screen is the /games list. Up/Down or left stick selects a folder;
A launches it. Hold a direction to scroll. X refreshes the list; PLUS exits idle.
The MKW requirement remains. No title-specific build or prebuilt RomFS map is
needed. The old /games/nx2-yoshi metadata folder on SD is unused and can be removed.

EXTRACTED FOLDER LAYOUTS
/games/Game Name/exefs/{rtld,main,subsdk*,sdk,main.npdm}
/games/Game Name/romfs/...
or the same two directories under /games/Game Name/base/.
The list uses folder names, sorted alphabetically. Incomplete folders without
required modules/RomFS are excluded. An updates directory is not merged: the
screen identifies base-only selection when such a directory is present.
Encrypted containers, compressed packages and update overlays are not supported.

The selected game's directory is indexed on each launch without copying assets.
Large folders show file-count progress; scanning hundreds of thousands of files
on SD can take several minutes. The current cap is 262144 files/directories,
128 MiB of index data, 13 modules (rtld/main/up to ten subsdks/sdk), and a 64-level
folder depth. Unsupported layouts or SDK wrapper patterns stop with a report.

GENERAL LOADER
Modules are discovered from the selected folder. SDK interfaces/entry points
are resolved by exports; service/memory syscall gates are resolved by verified
wrapper instructions, not game hashes or fixed game-code offsets. Native rtld
performs relocation and startup. The game uses an SD-backed RomFS view and private
SD saves. File handles use a bounded 16-entry cache, independent of file count.
No ticket/signature, ownership database, installed-game or HEN files are changed.

This is a general launch attempt for compatible extracted titles, not a promise
that every title works. SDK interfaces/wrapper layouts, original title services,
MKW's permissions/resources, legacy SetHeapSize games, graphics and other runtime
assumptions can still prevent a title from starting. Refusals are recorded in
/nx2-launch-report.txt; share that report with the selected title and observations.

GAMEPLAY CUTOFF REMOVED
The 0.12 successful Yoshi report renders through 175 seconds; its independent
180-second diagnostic timer explains the reported closure at roughly three minutes.
0.13 has no gameplay watchdog or timed process exit. The five-minute startup
limit applies only before the instrumented main entry. Use normal HOME/Close
Software to end the running title; the old L+R+PLUS+MINUS control is unavailable.

SAVES
Private SD saves are isolated by the selected NPDM program ID, or a folder hash
when no valid ID is available, under /switch/nx2-backup-loader/saves/.
The previously working Yoshi demo keeps its existing yoshi-demo save directory.
The system still identifies the running host as MKW. Save bridge coverage remains
limited; unapproved native FSP requests stop execution rather than using NAND.

VALIDATION
Yoshi was observed by the user reaching menus and gameplay with 0.12. The generic
resolver reproduces its 68 working hook sites. PC checks also prepare Zelda's
four modules and resolve its 20 SDK bindings and 68 hooks without title-specific
code. Yoshi's generated index matches the prior index byte-for-byte; Zelda's
186587-file index builds and validates. These are preparation checks, not a new
console gameplay test. 0.13's browser, general startup and games beyond Yoshi
still need hardware verification. Copy the report before launching another game.
