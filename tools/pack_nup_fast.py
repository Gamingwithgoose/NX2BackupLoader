#!/usr/bin/env python3
"""Pack a game for NX2BackupLoader's current random-access NUP reader.

Requires the a55uka/nup Python reference package:
    py -m pip install <path-to-cloned-nup>/tools/nup
Example:
    py tools/pack_nup_fast.py YoshiAndTheMysteriousBook_Demo Yoshi-fast.nup \
        --name "Yoshi and the Mysterious Book Demo"

NUP v1 stores each file as one Zstd frame or raw. The current native reader
cannot seek into a compressed frame. For files above the threshold this tool
selects the standard RAW codec, preserving offset-based random reads without
changing the NUP format or bypassing any checksum verification.
"""
from __future__ import annotations

import argparse
import importlib
import os
from types import SimpleNamespace


class _StoreRaw:
    """Makes the upstream packer take its existing safe RAW fallback."""
    def compress(self, data: bytes) -> bytes:
        return data

    def flush(self) -> bytes:
        return b""


class _SelectiveCompressor:
    def __init__(self, native: object, threshold: int):
        self.native = native
        self.threshold = threshold

    def compress(self, data: bytes) -> bytes:
        if len(data) > self.threshold:
            return data
        return self.native.compress(data)

    def compressobj(self, size: int):
        if size > self.threshold:
            return _StoreRaw()
        return self.native.compressobj(size)


def main() -> None:
    p = argparse.ArgumentParser(description="Pack a random-access-friendly NUP")
    p.add_argument("source", help="Extracted content directory or game root")
    p.add_argument("output", help="New .nup file (must not already exist)")
    p.add_argument("--name", help="Display name in the NUP metadata")
    p.add_argument("--raw-above-mib", type=int, default=16,
                   help="Store files larger than this uncompressed (default 16 MiB)")
    p.add_argument("--level", type=int, default=3)
    args = p.parse_args()
    if args.raw_above_mib < 1:
        p.error("--raw-above-mib must be at least 1")
    if os.path.exists(args.output):
        p.error("Output already exists; choose a new filename")
    pack_module = importlib.import_module("nup.pack")
    native_zstd = pack_module.zstandard
    if native_zstd is None:
        p.error("Install zstandard first: py -m pip install zstandard")
    original_factory = native_zstd.ZstdCompressor
    threshold = args.raw_above_mib * 1024 * 1024
    pack_module.zstandard = SimpleNamespace(
        ZstdCompressor=lambda **kw: _SelectiveCompressor(
            original_factory(**kw), threshold
        )
    )

    def progress(done, total, _):
        if done == total or done % 25 == 0:
            print(f"Packed {done}/{total} files", flush=True)

    result = pack_module.pack(
        args.output, args.source, level=args.level,
        names={None: args.name} if args.name else None,
        progress=progress,
    )
    print(f"Wrote {args.output} ({len(result)} package(s))")
    print(f"Files larger than {args.raw_above_mib} MiB are stored raw for fast seeking.")
    print("Run: py -m nup.cli verify --fast " + repr(args.output))


if __name__ == "__main__":
    main()
