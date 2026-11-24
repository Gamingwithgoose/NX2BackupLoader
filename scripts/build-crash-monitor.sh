#!/usr/bin/env bash
set -euo pipefail
cd "$(cygpath -u "$NX2_PROJECT_ROOT")"
if [[ "${DEVKITPRO:-}" == *:* ]]; then DEVKITPRO="$(cygpath -u "$DEVKITPRO")"; fi
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
rm -f release/kaihen/plugins/nx2-crash.nso
make -C plugins/crash-monitor clean
make -C plugins/crash-monitor
mkdir -p release/kaihen/plugins
cp plugins/crash-monitor/nx2-crash.nso release/kaihen/plugins/
touch release/kaihen/nx2-monitor.flag

cp plugins/crash-monitor/nx2-crash.ini release/kaihen/nx2-crash.ini
