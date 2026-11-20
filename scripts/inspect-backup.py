#!/usr/bin/env python3
"""Read-only PC preflight for an extracted ExeFS/RomFS backup; never launches code."""
import argparse
import json
import os
import re
import struct
import sys
from pathlib import Path
from npdm_policy import read_policy


def inspect_nso(path):
    result = {"file": path.name, "errors": [], "segments": []}
    try:
        with path.open("rb") as stream:
            size = path.stat().st_size
            header = stream.read(0x100)
        result["bytes"] = size
        if len(header) != 0x100 or header[:4] != b"NSO0":
            result["errors"].append("Missing complete 256-byte NSO0 header")
            return result
        flags = struct.unpack_from("<I", header, 0xC)[0]
        result["build_id"] = header[0x40:0x60].hex()
        ranges = []
        memory = []
        for i, name in enumerate(("text", "rodata", "data")):
            offset, address, unpacked = struct.unpack_from("<III", header, 0x10 + 16*i)
            compressed = bool(flags & (1 << i))
            stored = struct.unpack_from("<I", header, 0x60 + 4*i)[0] if compressed else unpacked
            result["segments"].append({"name": name, "offset": offset,
                "stored_bytes": stored, "memory_bytes": unpacked, "compressed": compressed})
            if unpacked and (not stored or offset < 0x100 or offset + stored > size):
                result["errors"].append(f"{name}: stored range missing or outside file")
            if not unpacked and stored:
                result["errors"].append(f"{name}: stored bytes without memory size")
            if stored:
                ranges.append((offset, offset + stored, name))
            if unpacked:
                memory.append((address, address + unpacked, name))
                if address + unpacked > 2**32:
                    result["errors"].append(f"{name}: memory range exceeds 32-bit NSO offsets")
            if i == 0 and not unpacked:
                result["errors"].append("text: executable segment is empty")
        for label, entries in (("file", ranges), ("memory", memory)):
            ordered = sorted(entries)
            for previous, current in zip(ordered, ordered[1:]):
                if current[0] < previous[1]:
                    result["errors"].append(f"Overlapping {label} segments: {previous[2]}, {current[2]}")
    except OSError as exc:
        result["errors"].append(str(exc))
    return result


def inspect_inventory(root):
    result = {"files": 0, "bytes": 0, "listing_files": 0, "listed_files_checked": 0, "errors": []}
    def walk_error(exc):
        result["errors"].append(str(exc))
    for directory, _, files in os.walk(root, onerror=walk_error, followlinks=False):
        directory = Path(directory)
        for name in files:
            path = directory / name
            try:
                if name != ".listing":
                    result["files"] += 1
                    result["bytes"] += path.stat().st_size
                    continue
                result["listing_files"] += 1
                for line in path.read_text(encoding="utf-8").splitlines():
                    if not line.strip():
                        continue
                    fields = line.split(None, 8)
                    if len(fields) != 9 or fields[0][0] not in "d-":
                        result["errors"].append(f"Unrecognized listing row in {path}")
                        continue
                    leaf = fields[8]
                    if leaf in (".", ".."):
                        continue
                    if "/" in leaf or "\\" in leaf or ":" in leaf:
                        result["errors"].append(f"Unsafe listing name in {path}")
                        continue
                    target = directory / leaf
                    if fields[0][0] == "d":
                        if not target.is_dir():
                            result["errors"].append(f"Listed directory missing: {target}")
                    else:
                        result["listed_files_checked"] += 1
                        if not target.is_file() or target.stat().st_size != int(fields[4]):
                            result["errors"].append(f"Listed file missing or size mismatch: {target}")
            except (OSError, UnicodeError, ValueError) as exc:
                result["errors"].append(f"{path}: {exc}")
    return result


def inspect_backup(root, inventory=False):
    root = Path(root).resolve()
    requested = root
    if not (root / "exefs").is_dir() and (root / "base/exefs").is_dir():
        root = root / "base"
    exefs, romfs = root / "exefs", root / "romfs"
    result = {"requested_folder": str(requested), "folder": str(root), "launchability": "UNDETERMINED",
        "errors": [], "modules": [], "limitations": [
            "Does not launch code, contact a console, decrypt, or alter backup files.",
            "NSO checks cover headers and ranges; compression and segment hashes are not verified.",
            "NPDM declarations are parsed; policy signatures and effective process permissions are not verified.",
            "RomFS directory presence does not prove completeness or correct base/update pairing.",
            "Firmware, licenses, runtime services, and native SD launch support are not established."]}
    try:
        if not exefs.is_dir():
            result["errors"].append("Missing exefs directory")
        if not romfs.is_dir():
            result["errors"].append("Missing romfs directory")
        npdm = exefs / "main.npdm"
        try:
            result["policy"] = read_policy(npdm)
        except (OSError, ValueError) as exc:
            result["errors"].append(f"main.npdm: {exc}")
        if not (exefs / "main").is_file():
            result["errors"].append("Missing main executable")
        if exefs.is_dir():
            for path in sorted(exefs.iterdir()):
                if re.fullmatch(r"main|rtld|sdk|subsdk[0-9]+", path.name):
                    module = inspect_nso(path)
                    result["modules"].append(module)
                    result["errors"].extend(f"{path.name}: {e}" for e in module["errors"])
    except OSError as exc:
        result["errors"].append(f"Directory inspection failed: {exc}")
    if inventory:
        result["inventory"] = inspect_inventory(root)
        result["errors"].extend(result["inventory"]["errors"])
    result["preflight"] = "ISSUES_FOUND" if result["errors"] else "BASIC_CHECKS_PASSED"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path, help="One game folder containing exefs/ and romfs/")
    parser.add_argument("--inventory", action="store_true", help="Count files/bytes and compare FTP .listing file sizes (no hashing)")
    args = parser.parse_args()
    result = inspect_backup(args.folder, inventory=args.inventory)
    # Output to stdout only; a caller can save it outside the backup directory.
    print(json.dumps(result, indent=2, ensure_ascii=True))
    return 1 if result["errors"] else 0


if __name__ == "__main__":
    sys.exit(main())
