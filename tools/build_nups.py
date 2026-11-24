#!/usr/bin/env python3
"""Build one random-access-friendly standard NUP per immediate extracted/ child."""
from __future__ import annotations

import importlib.util
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
EXTRACTED = ROOT / "extracted"
OUTPUT = ROOT / "nups"
PACKER = ROOT / "tools" / "pack_nup_fast.py"


def main() -> int:
    EXTRACTED.mkdir(exist_ok=True)
    OUTPUT.mkdir(exist_ok=True)
    folders = sorted((x for x in EXTRACTED.iterdir() if x.is_dir()),
                     key=lambda x: x.name.casefold())
    if not folders:
        print("[INFO] No game folders found in extracted/.")
        print(f"[INFO] Place extracted/Your Game/exefs and romfs there,")
        print("       or extracted/Your Game/base/exefs and base/romfs.")
        return 0
    good = failed = skipped = 0
    for index, folder in enumerate(folders, start=1):
        target = OUTPUT / f"{folder.name}.nup"
        if target.exists():
            print(f"[SKIP] {target.name} already exists; will not overwrite.")
            skipped += 1
            continue
        print(f"\n[{index}/{len(folders)}] Packing {folder.name}", flush=True)
        cmd = [sys.executable, str(PACKER), str(folder), str(target),
               "--name", folder.name]
        try:
            result = subprocess.run(cmd, check=False)
        except OSError as exc:
            print(f"[ERROR] Cannot start packer for {folder.name}: {exc}")
            failed += 1
            continue
        if result.returncode != 0:
            print(f"[ERROR] Packing failed for {folder.name}; deleting incomplete output.")
            try:
                target.unlink(missing_ok=True)
            except OSError as exc:
                print(f"[WARN] Could not delete incomplete {target}: {exc}")
            failed += 1
            continue
        # Fast validation checks archive structure and table integrity without
        # reading/decompressing every multi-gigabyte asset a second time.
        print(f"[VERIFY] {target.name}", flush=True)
        verify = subprocess.run([sys.executable, "-m", "nup.cli",
                                 "verify", str(target), "--fast"],
                                check=False)
        if verify.returncode:
            print(f"[ERROR] Verification failed for {folder.name}; removing output.")
            try:
                target.unlink(missing_ok=True)
            except OSError as exc:
                print(f"[WARN] Could not delete invalid {target}: {exc}")
            failed += 1
            continue
        print(f"[OK] {target.name}", flush=True)
        good += 1
    print(f"\n[SUMMARY] Built: {good}  Skipped existing: {skipped}  Failed: {failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
