#!/usr/bin/env bash
set -euo pipefail
if [[ -z "${NX2_PROJECT_ROOT:-}" ]]; then echo "[ERROR] NX2_PROJECT_ROOT missing"; exit 1; fi
cd "$(cygpath -u "$NX2_PROJECT_ROOT")"
if [[ ! -f probes/game-launch/Makefile || ! -f scripts/verify-nro.ps1 ]]; then echo "[ERROR] Not an NX2BackupLoader project directory"; exit 1; fi
# A failed direct invocation must not leave an earlier candidate looking current.
trap 'rm -f release/switch/BackupLoaderMKW/BackupLoaderMKW.nro release/switch/BackupLoaderMKW/BackupLoaderMKW.nro.tmp release/switch/BackupLoaderMKW/runtime/BackupLoaderMKW-core.nro release/switch/BackupLoaderMKW/runtime/BackupLoaderMKW-core.nro.tmp' ERR
rm -f release/switch/BackupLoaderMKW/BackupLoaderMKW.nro release/switch/BackupLoaderMKW/BackupLoaderMKW.nro.tmp release/switch/BackupLoaderMKW/runtime/BackupLoaderMKW-core.nro release/switch/BackupLoaderMKW/runtime/BackupLoaderMKW-core.nro.tmp
if [[ "${DEVKITPRO:-}" == *:* ]]; then DEVKITPRO="$(cygpath -u "$DEVKITPRO")"; fi
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
if [[ "${DEVKITA64:-}" == *:* ]]; then DEVKITA64="$(cygpath -u "$DEVKITA64")"; fi
export DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"
export PATH="$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH"
if [[ ! -f "$DEVKITPRO/libounce/switch2_rules" ]]; then
 echo "[ERROR] Switch 2 ounce-devkit/libounce not installed."
 echo "[ERROR] Refusing to build an incompatible Switch 1 NRO."
 echo "Official setup: https://github.com/ouncebrew/ounce-devkit"
 echo "Do not copy any older NRO to your Switch 2."
 exit 1
fi
if [[ ! -f "$DEVKITPRO/libounce/switch.specs" ]]; then
 echo "[ERROR] Missing libounce Switch 2 startup/specs. Fix ounce-devkit installation."
 exit 1
fi
if ! command -v make >/dev/null 2>&1; then echo "[ERROR] make missing"; exit 1; fi
echo "[INFO] Native Switch 2 libounce rules found: $DEVKITPRO/libounce/switch2_rules"
make -C probes/game-launch -j2
test -s probes/game-launch/BackupLoaderMKW.nro
make -C probes/library-ui -j2
test -s probes/library-ui/BackupLoaderMKW-UI.nro
if ! command -v powershell.exe >/dev/null 2>&1; then echo "[ERROR] Windows PowerShell verifier missing"; exit 1; fi
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$PWD/scripts/package-current-test.ps1")" -Root "$(cygpath -w "$PWD")"
