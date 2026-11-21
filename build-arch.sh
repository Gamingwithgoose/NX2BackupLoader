#!/usr/bin/env bash
# Native Arch Linux / Linux builder for the libounce Switch 2 NRO.
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$ROOT"
finish() {
  local status=$?
  if (( status != 0 )); then
    echo
    echo "[FAILED] NRO not rebuilt/packaged (exit $status)."
    echo "[NOTE] An older release may still exist; do not mistake it for this build."
  fi
  if [[ "${NX2_KEEP_OPEN:-0}" == 1 && -t 0 ]]; then
    echo
    read -r -p "Press Enter to close this window..." _ || true
  fi
}
trap finish EXIT
echo "=========================================================="
echo "BackupLoaderMKW - Arch Linux / Switch 2 NRO Builder"
echo "=========================================================="
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"
export PATH="$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH"
if [[ ! -f probes/game-launch/Makefile || ! -f scripts/package-current-test.py ]]; then
  echo "[ERROR] Keep this script in the repository root."
  exit 1
fi
for tool in make python3; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "[ERROR] Missing $tool. On Arch: sudo pacman -S base-devel python"
    exit 1
  fi
done
if [[ ! -f "$DEVKITPRO/libounce/switch2_rules" ||
      ! -f "$DEVKITPRO/libounce/switch.specs" ]]; then
  echo "[ERROR] Switch 2 ounce-devkit/libounce not found in $DEVKITPRO."
  echo "[SETUP] https://github.com/ouncebrew/ounce-devkit"
  echo "[NOTE] No automatic system-wide SDK installation or Switch 1 fallback."
  exit 1
fi
if [[ ! -f assets/BackupLoaderMKW.jpg ]]; then
  echo "[ERROR] Missing Homebrew Menu JPEG: assets/BackupLoaderMKW.jpg"
  exit 1
fi
echo "[BUILD] Native Switch 2 NRO with libounce..."
rm -f probes/game-launch/BackupLoaderMKW.nro
make -C probes/game-launch -j2
test -s probes/game-launch/BackupLoaderMKW.nro
echo "[VERIFY/PACKAGE] Switch 2 header validation and JPEG ASET embedding..."
python3 scripts/package-current-test.py --root "$ROOT"
echo
echo "[SUCCESS] $ROOT/release/switch/BackupLoaderMKW/BackupLoaderMKW.nro"
echo "Copy the contents of release/switch to the SD card root."
