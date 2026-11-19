# NX2BackupLoader

An experimental, **read-only first-stage** Switch 2 homebrew diagnostic for decrypted game-folder layouts. It is **not yet a retail game loader**, game installer, or license backup solution.

## Quick start (Windows local build)

1. Install devkitPro Switch development packages (devkitA64, libnx, switch-dev) in the default `C:\devkitPro` directory. This project does not use GitHub Actions.
2. Pull the repository with GitHub Desktop.
3. Double-click `build.bat` at the repository root. It calls the local devkitPro MSYS2 toolchain and fails explicitly if dependencies are missing.
4. Copy `release/nx2backuploader.nro` to `sdmc:/switch/nx2backuploader.nro`.
5. Put **one test folder** at `sdmc:/games/MyGame/exefs/` and `sdmc:/games/MyGame/romfs/`; ExeFS should contain `main`, `main.npdm` and, if available, `rtld` / `sdk`.
6. Run from your existing kaiHEN homebrew launcher. Use Up/Down to select, A to inspect, X to rescan, + to exit.
7. Retrieve `sdmc:/nx2backuploader-report.txt` and use it to evaluate the next implementation stage.

## Current behavior

- Enumerates subdirectories of `sdmc:/games` with bounded names and entries.
- Checks ExeFS and RomFS folder presence, verifies four-byte `META` and `NSO0` headers, and records file size.
- Writes a report to the SD card only when you press A.
- Does **not** load NSOs, mount game RomFS, hook system services, alter licensing, or modify installed content.

## Roadmap

- Parse full NPDM/NSO layout, dependency records and segment bounds.
- Probe permissions and service availability without changing content or HEN.
- Test an independently built sample module in a controlled loader.
- Investigate native execution and filesystem redirection separately; no promise of compatibility for retail Switch 2 software.

## Compatibility and caution

Built with standard devkitPro/libnx NRO tooling. Compatibility with a particular Switch 2 kaiHEN revision, service access, and hardware has **not** been established. This is intended to be the first hardware diagnostic. Do not delete installed games, factory reset the console, or overwrite working HEN files on the basis of a successful diagnostic.

## Changelog

### v0.1 — first local diagnostic build
- Windows `build.bat` and reproducible devkitPro build.
- SD folder scanner, basic ExeFS header validation, console UI and report output.
- Local `release/` output; no Actions, no pull request workflow.
