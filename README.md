# BackupLoaderMKW

> **USE AT OWN RISK.** I am not responsible for bricks that may or may not occur as this is still in early development and experimental. Back up your SD card and saves first.

BackupLoaderMKW is an experimental **Switch 2** homebrew launcher for extracted backups. It uses **Mario Kart World's title override** because Mario Kart World provides more memory for homebrew. The console still sees MKW as the host.

**Only Yoshi and the Mysterious Book Demo has been confirmed to reach gameplay.** Other games still need testing. Some will crash or simply not work. No games are included.

## What you need

- A Switch 2 running compatible kaiHEN/Homebrew Menu software.
- Mario Kart World and its **title override** for launching Homebrew Menu (not Album mode).
- Your own extracted game files and an SD card.

**Experimental branch:** NUP files can now be recognized and listed, but cannot be launched yet. Do not expect playable NUP support in this build. Encrypted containers are unsupported.

## Put games on the SD card

Create a folder called **games** at the very root of the SD card. Each game gets its own folder containing **exefs** and **romfs**.

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
5. Select an extracted game using Up/Down or the left stick. Press **A** to launch, **X** to refresh the list, or **+** to exit. Hold **B** while pressing **A** to rebuild an extracted game’s index. While a large folder is being indexed, press **B** to cancel and return to the game list. Cancelled scans do not yet resume.
6. Use HOME / Close Software to stop playing.

If the list is empty, confirm the game folders contain real exefs and romfs directories. Large backups may take several minutes to index the first time. Completed indexes are saved in SD:/switch/BackupLoaderMKW/cache/ so later launches can skip the file-by-file scan. The cache checks the top-level romfs folder timestamp; changes inside nested folders may require **B+A** to rebuild it. NUP files appear in the list if their header and table of contents pass initial checks, but selecting one only shows a development notice.

## Where saves go

Each game now gets its own folder:

    SD:/switch/BackupLoaderMKW/(game folder name)/

The folder name is the same one shown under SD:/games. Renaming the game folder changes its save location.

**Coming from NX2BackupLoader?** Old saves are still under SD:/switch/nx2-backup-loader/saves/. This update does **not** move or delete them automatically. Back them up, then copy the old save folder's contents into the new game-named folder. The previously working Yoshi demo used the old **yoshi-demo** save folder. Keep your old saves until you've checked the new copy.

## Gameplay / crash log

Every launch writes one log to:

    SD:/BackupLoaderMKW-gameplay.log

If a backup crashes, copy this file **before launching something else**, because the next launch replaces it. It records startup, game progress, and errors. An abrupt system crash may prevent the final lines from being saved. No other SD launch reports are produced.

## How it works

The loader discovers the selected backup's executable modules and files. It connects supported game SDK calls, lets the game's original startup code run, and handles supported game file and save requests through the SD card. Mario Kart World remains the system's host title, so the loader has MKW's memory allocation and permissions — not necessarily every resource other games require.

It does not install game tickets, rewrite game ownership, or install backups to NAND. The working game launcher currently reads loose files from SD. Different titles have different services, graphics, and memory needs, so there is no promise that they will all launch or save correctly.

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

Only **Yoshi and the Mysterious Book Demo** is confirmed to work in gameplay. NUP discovery is experimental; direct NUP launch and other packaged/encrypted backups are unsupported. Updates are not merged. Some games require services or memory that MKW does not have. The startup timeout is five minutes before game main entry, but there is no gameplay timer.

**Early experimental release. Use at your own risk.**
