#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
aarch64-none-elf-gcc -O2 -fPIC -ffreestanding -fno-stack-protector -nostdlib -shared \
  -Wl,-T,fixture/module.ld -Wl,--hash-style=sysv -Wl,-z,max-page-size=4096 \
  -Wl,-e,nx2_module_entry fixture/module.c -o fixture/module.elf
python fixture/package.py
