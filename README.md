# NX2BackupLoader

**Switch 2 / kaiHEN experimental game-folder diagnostic. Native game execution is NOT implemented.**

## IMPORTANT: Toolchain correction (v0.2)

The earlier v0.1 build script incorrectly compiled with Switch 1 `libnx/switch_rules` and `libnx/switch.specs`. Its NRO is **not considered safe or compatible** with Switch 2 and should not be launched. That build caused a reported console crash and loss of the temporary kaiHEN session. Do not use it.

The builder now **requires ouncebrew's official Switch 2 [ounce-devkit](https://github.com/ouncebrew/ounce-devkit)** and `libounce/switch2_rules`. It checks the NRO header and Switch 2 `OUN0` marker, and rejects an older Switch 1 output. Header verification is **not** hardware compatibility testing.

### Local Windows build

1. Install devkitPro and the [ounce-devkit](https://github.com/ouncebrew/ounce-devkit) Switch 2 packages (libounce, OunceVK, libkaihen) following upstream instructions. Do not replace your working console kaiHEN files; this is the PC compilation toolchain.
2. Pull latest changes in GitHub Desktop.
3. Run `build.bat` from the repo root. It stays open and writes `build.log`.
4. Only on success, a candidate NRO is created at `release/nx2backuploader.nro`.

**IMPORTANT: This v0.2 candidate has not been compiled or tested on Switch 2 hardware by this repository. Do not risk a working HEN session solely on its source-code validation.**

### Diagnostic scope (no installation or game execution)

Read-only scanner expects folders such as `sdmc:/games/MyGame/exefs/main`, `main.npdm`, optional `rtld`/`sdk`, and `sdmc:/games/MyGame/romfs/`.

- Up/Down: select; A: write report; X: scan; +: exit
- Report: `sdmc:/nx2backuploader-report.txt`
- No game loading, NAND writes, ticket work, or license changes.

## Changelog

### v0.2
- **Critical:** native Switch 2 libounce toolchain enforcement and fail-closed output handling.
- Native Switch 2 rules/specs and architecture; checks for OUN0 marker.
- Build script stays open, logs output, and cannot silently reuse an older Switch 1 NRO.

### v0.1 (unsafe/retired)
- Original SD game-folder scanner built using Switch 1 libnx; do not launch that output.
