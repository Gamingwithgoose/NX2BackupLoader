#!/usr/bin/env python3
"""Read-only NUP-style zstd/dedup estimate; writes no archive. Python 3.14+."""
import argparse
import hashlib
import json
import os
import sys
from pathlib import Path


def measure(root, level=3):
    from compression import zstd
    root = Path(root).resolve()
    if not root.is_dir():
        raise ValueError("Backup directory missing")
    totals = {"folder": str(root), "files": 0, "source_bytes": 0,
        "unique_payload_bytes": 0, "deduplicated_files": 0, "level": level,
        "archive_created": False, "launchability": "UNDETERMINED", "files_measured": []}
    seen = {}
    def walk_error(exc):
        raise exc
    for directory, dirs, files in os.walk(root, followlinks=False, onerror=walk_error):
        dirs.sort()
        files.sort()
        if any((Path(directory)/d).is_symlink() for d in dirs):
            raise ValueError("Symbolic-link directory unsupported in backup")
        for name in files:
            if name == ".listing":
                continue
            path = Path(directory)/name
            if path.is_symlink():
                raise ValueError("Symbolic-link file unsupported in backup")
            before = path.stat()
            compressor = zstd.ZstdCompressor(level=level)
            digest = hashlib.sha256()
            compressed, size = 0, 0
            with path.open("rb") as stream:
                while chunk := stream.read(1024*1024):
                    digest.update(chunk)
                    size += len(chunk)
                    compressed += len(compressor.compress(chunk))
                compressed += len(compressor.flush())
            after = path.stat()
            if size != before.st_size or (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                raise ValueError(f"File changed during measurement: {path}")
            stored = min(size, compressed)
            identity = (digest.digest(), size)
            duplicate = identity in seen
            if duplicate:
                totals["deduplicated_files"] += 1
            else:
                seen[identity] = stored
                totals["unique_payload_bytes"] += stored
            totals["source_bytes"] += size
            totals["files"] += 1
            totals["files_measured"].append({"path": path.relative_to(root).as_posix(),
                "source_bytes": size, "estimated_stored_bytes": stored, "duplicate": duplicate})
            print(f"Measured {totals['files']} files, {totals['source_bytes']:,} source bytes", file=sys.stderr)
    totals["payload_savings_percent"] = round(100*(1-totals["unique_payload_bytes"]/totals["source_bytes"]),2) if totals["source_bytes"] else 0
    totals["limitations"] = ["Payload estimate only; excludes TOC, metadata, alignment and container overhead.",
        "Streaming zstd settings can differ from the NUP reference packer; exact archive size requires an actual pack.",
        "A smaller archive does not establish efficient random reads or on-console game loading."]
    return totals


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--level", type=int, default=3)
    args = parser.parse_args()
    try:
        result = measure(args.folder, args.level)
    except (OSError, ValueError, ImportError) as exc:
        print(json.dumps({"error": str(exc), "archive_created": False}, indent=2))
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
