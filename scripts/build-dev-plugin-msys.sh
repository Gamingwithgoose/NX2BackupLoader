#!/usr/bin/env bash
set -euo pipefail
if [[ -z "${NX2_PROJECT_ROOT:-}" ]]; then echo "[ERROR] NX2_PROJECT_ROOT missing"; exit 1; fi
cd "$(cygpath -u "$NX2_PROJECT_ROOT")"
if [[ "${DEVKITPRO:-}" == *:* ]]; then DEVKITPRO="$(cygpath -u "$DEVKITPRO")"; fi
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export PATH="$DEVKITPRO/tools/bin:$DEVKITPRO/devkitA64/bin:$PATH"
target=release/developer/kaihen/plugins/nx2trace.nso
rm -f "$target"
test -f "$DEVKITPRO/libounce/switch2_cpu.mk"
test -f "$DEVKITPRO/libounce/lib/libkaihen_plugin.a"
test -f "$DEVKITPRO/libounce/lib/libounce_core.a"
make -C diagnostics/nx2trace clean
make -C diagnostics/nx2trace -j2
test -s diagnostics/nx2trace/nx2trace.nso
mkdir -p "$(dirname "$target")"
cp diagnostics/nx2trace/nx2trace.nso "$target"
echo "[INFO] Private plugin built: $target"
