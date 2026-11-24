#!/usr/bin/env bash
# Native Arch Linux / Linux NUP batch packer.
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$ROOT"
finish() {
  local status=$?
  if (( status != 0 )); then
    echo
    echo "[FAILED] NUP build exited with code $status."
    echo "[NOTE] Existing completed NUPs were not overwritten."
  fi
  if [[ "${NX2_KEEP_OPEN:-0}" == 1 && -t 0 ]]; then
    echo
    read -r -p "Press Enter to close this window..." _ || true
  fi
}
trap finish EXIT
echo "=========================================================="
echo "BackupLoaderMKW - Arch Linux / Batch NUP Builder"
echo "=========================================================="
echo "Input : $ROOT/extracted"
echo "Output: $ROOT/nups"
echo
if ! command -v python3 >/dev/null 2>&1; then
  echo "[ERROR] Python 3 missing. On Arch: sudo pacman -S python"
  exit 1
fi
if [[ ! -f tools/build_nups.py || ! -f tools/pack_nup_fast.py ]]; then
  echo "[ERROR] Missing NUP build tools; keep repository folders together."
  exit 1
fi
# Arch's externally managed system Python must not be modified.
VENV="$ROOT/.venv-nup-arch"
PY="$VENV/bin/python"
if [[ ! -x "$PY" ]]; then
  echo "[SETUP] Creating isolated local Python venv..."
  python3 -m venv "$VENV" || {
    echo "[ERROR] Could not create a Python venv. Check Python/ensurepip."
    exit 1
  }
fi
if ! "$PY" -c 'import nup.pack, zstandard' >/dev/null 2>&1; then
  if ! command -v git >/dev/null 2>&1; then
    echo "[ERROR] Git is required. On Arch: sudo pacman -S git"
    exit 1
  fi
  echo "[SETUP] Installing upstream NUP packer and zstandard in local venv..."
  "$PY" -m pip install --disable-pip-version-check zstandard \
    'git+https://github.com/a55uka/nup.git@meow#subdirectory=tools/nup'
  "$PY" -c 'import nup.pack, zstandard'
fi
mkdir -p extracted nups
echo "[BUILD] Packing extracted games as random-access-friendly NUP archives..."
"$PY" tools/build_nups.py
echo
echo "[DONE] Generated archives: $ROOT/nups"
echo "Copy selected .nup files to SD:/games."
