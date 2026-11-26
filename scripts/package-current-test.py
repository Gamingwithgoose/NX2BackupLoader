#!/usr/bin/env python3
"""Native Linux replacement for the PowerShell verifier + release packager."""
from __future__ import annotations
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import struct

def u32(data: bytes, off: int) -> int:
    if off < 0 or off + 4 > len(data):
        raise ValueError(f"Truncated NRO field {off:#x}")
    return struct.unpack_from("<I", data, off)[0]

def verify(data: bytes) -> int:
    if not 128 <= len(data) <= 64 * 1024 * 1024:
        raise ValueError("NRO size outside supported 128-byte to 64 MiB range")
    if data[0x10:0x14] != b"NRO0" or u32(data, 0x14) != 0:
        raise ValueError("NRO0 header missing or version unsupported")
    image_size = u32(data, 0x18)
    if not 128 <= image_size <= len(data):
        raise ValueError("Invalid NRO image size")
    end = text_size = 0
    for segment in range(3):
        off, size = u32(data, 0x20 + 8 * segment), u32(data, 0x24 + 8 * segment)
        if off % 4096 or size % 4096 or off < end or off + size > image_size:
            raise ValueError(f"Invalid alignment/overlap/bounds for segment {segment}")
        if segment == 0:
            if off != 0 or size < 128:
                raise ValueError("Invalid NRO text segment")
            text_size = size
        end = off + size
    if end != image_size:
        raise ValueError("NRO segments do not span image")
    module, startup_end = u32(data, 4), min(4096, text_size)
    if module < 128 or module % 4 or module + 28 > startup_end:
        raise ValueError("Invalid MOD0 offset")
    if data[module:module + 4] != b"MOD0":
        raise ValueError("MOD0 record missing")
    rec = module + 28
    for extension in (b"LNY0", b"LNY1", b"LNY2"):
        if rec + 12 <= startup_end and data[rec:rec + 4] == extension:
            rec += 12
    if rec + 12 > startup_end or data[rec:rec + 4] != b"OUN0":
        raise ValueError("Switch 2 OUN0 startup record missing")
    if u32(data, rec + 4) != 2 or u32(data, rec + 8) != 1:
        raise ValueError("Not a supported Switch 2 OUN0 build")
    if image_size != len(data):
        raise ValueError("NRO source already contains trailing assets")
    print("[OK] Switch 2 NRO0/MOD0/OUN0 headers and segment bounds verified")
    print(f"[OK] SHA256: {hashlib.sha256(data).hexdigest()}")
    return image_size

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, required=True)
    root = ap.parse_args().root.resolve()
    source = root / "probes/library-ui/BackupLoaderMKW-UI.nro"
    core_source = root / "probes/game-launch/BackupLoaderMKW.nro"
    icon = root / "assets/BackupLoaderMKW.jpg"
    release = root / "release"
    target = release / "switch/BackupLoaderMKW/BackupLoaderMKW.nro"
    stage = target.with_name(target.name + ".tmp")
    core_target = target.parent / "runtime/BackupLoaderMKW-core.nro"
    core_stage = core_target.with_name(core_target.name + ".tmp")
    data = source.read_bytes()
    image_size = verify(data)
    core_data = core_source.read_bytes()
    verify(core_data)
    jpg = icon.read_bytes()
    if not (16 <= len(jpg) <= 131072 and
            jpg.startswith(b"\xff\xd8") and jpg.endswith(b"\xff\xd9")):
        raise ValueError("Homebrew icon must be JPEG-formatted and at most 128 KiB")
    target.parent.mkdir(parents=True, exist_ok=True)
    core_target.parent.mkdir(parents=True, exist_ok=True)
    try:
        shutil.copyfile(core_source, core_stage)
        if hashlib.sha256(core_stage.read_bytes()).digest() != hashlib.sha256(core_data).digest():
            raise ValueError("Staged runtime checksum mismatch")
        shutil.copyfile(source, stage)
        if hashlib.sha256(stage.read_bytes()).digest() != hashlib.sha256(data).digest():
            raise ValueError("Staged NRO checksum mismatch")
        aset = bytearray(0x38)
        aset[:4] = b"ASET"
        struct.pack_into("<Q", aset, 8, 0x38)
        struct.pack_into("<Q", aset, 16, len(jpg))
        with stage.open("ab") as out:
            out.write(aset)
            out.write(jpg)
            out.flush()
            os.fsync(out.fileno())
        with stage.open("rb") as check:
            check.seek(image_size)
            header = check.read(0x38)
        if (header[:4] != b"ASET" or
                struct.unpack_from("<Q", header, 16)[0] != len(jpg)):
            raise ValueError("ASET icon verification failed")
        os.replace(core_stage, core_target)
        os.replace(stage, target)
    except Exception:
        target.unlink(missing_ok=True)
        core_target.unlink(missing_ok=True)
        raise
    finally:
        stage.unlink(missing_ok=True)
        core_stage.unlink(missing_ok=True)
    shutil.copy2(root / "README.md", release / "README.md")
    for old in (release / "switch/nx2-folder-exec",
                release / "nx2backuploader.nro", release / "nx2-sdprobe.nro"):
        if old.is_symlink():
            old.unlink()
        elif old.is_dir():
            shutil.rmtree(old)
        elif old.exists():
            old.unlink()
    print(f"[OK] Embedded Homebrew Menu icon ({len(jpg)} bytes)")
    print(f"[OK] Created {target}")
    print(f"[OK] Created {core_target}")
    print("[NOTE] Format validation does not prove hardware/gameplay compatibility")

if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, struct.error) as exc:
        raise SystemExit(f"[ERROR] NRO packaging failed: {exc}")
