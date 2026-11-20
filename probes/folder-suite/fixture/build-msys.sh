#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Fixtures deliberately use the standard AArch64 TPIDR_EL0 TLS dialect.
# The host NRO itself remains compiled with libounce's -mtp=soft flags.
for name in provider client; do
  aarch64-none-elf-gcc -O2 -fPIC -ffreestanding -fno-stack-protector -nostdlib -shared \
    -Wl,-T,fixture/module.ld -Wl,--hash-style=both -Wl,-z,pack-relative-relocs \
    -Wl,-z,max-page-size=4096 "fixture/$name.c" -o "fixture/$name.elf"
done
if [[ -n "${NX2_PYTHON:-}" ]]; then
  "$(cygpath -u "$NX2_PYTHON")" fixture/package_suite.py
elif command -v python >/dev/null 2>&1; then
  python fixture/package_suite.py
elif command -v python3 >/dev/null 2>&1; then
  python3 fixture/package_suite.py
else
  echo "[ERROR] Python 3 required to package test fixtures."; exit 1
fi
