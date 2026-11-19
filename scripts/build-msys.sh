#!/usr/bin/env bash
set -euo pipefail
if [[ -z "${NX2_PROJECT_ROOT:-}" ]]; then echo "[ERROR] NX2_PROJECT_ROOT was not supplied"; exit 1; fi
cd "$(cygpath -u "$NX2_PROJECT_ROOT")"
# devkitPro on Windows is ordinarily mounted at /c/devkitPro.
if [[ "${DEVKITPRO:-}" == *:* ]]; then DEVKITPRO="$(cygpath -u "$DEVKITPRO")"; fi
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
if [[ "${DEVKITA64:-}" == *:* ]]; then DEVKITA64="$(cygpath -u "$DEVKITA64")"; fi
export DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"
export PATH="$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH"
if [[ ! -f "$DEVKITPRO/libnx/switch_rules" ]]; then
 echo "[ERROR] Missing $DEVKITPRO/libnx/switch_rules"
 echo "Install devkitPro Switch packages (switch-dev, devkitA64 and libnx)."
 exit 1
fi
if ! command -v make >/dev/null 2>&1; then echo "[ERROR] make is missing"; exit 1; fi
make -C app clean
make -C app -j2
test -s app/nx2backuploader.nro
mkdir -p release
cp -f app/nx2backuploader.nro release/nx2backuploader.nro
echo "[OK] $(ls -lh release/nx2backuploader.nro)"
