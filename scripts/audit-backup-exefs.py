"""Read-only local ExeFS audit; never repairs or rewrites backup content.

Requires lz4 and, for ZBIC modules, a host library built from source/zbic.c
exporting nx2_zbic_decode. Emit metadata and decoded segment hash checks.
"""
import argparse
import ctypes
import hashlib
import json
import struct
from pathlib import Path

import lz4.block


def audit(directory, decoder):
    result = {"directory": str(directory), "modules": []}
    npdm = directory / "main.npdm"
    if npdm.is_file():
        data = npdm.read_bytes()
        if len(data) >= 128 and data[:4] == b"META":
            result["npdm"] = {
                "sha256": hashlib.sha256(data).hexdigest(),
                "priority": data[14], "core": data[15],
                "remote_resource_bytes_meta10": struct.unpack_from("<I", data, 16)[0],
                "resource_bytes": struct.unpack_from("<I", data, 20)[0],
                "stack_bytes": struct.unpack_from("<I", data, 28)[0],
            }
        else:
            result["npdm_error"] = "Invalid or truncated META header"
    for path in sorted(directory.iterdir()):
        if not path.is_file() or path.name == "main.npdm":
            continue
        data = path.read_bytes()
        if data[:4] != b"NSO0":
            continue
        module = {"name": path.name, "bytes": len(data),
                  "sha256": hashlib.sha256(data).hexdigest(), "segments": []}
        result["modules"].append(module)
        if len(data) < 256:
            module["error"] = "Truncated NSO header"
            continue
        flags = struct.unpack_from("<I", data, 12)[0]
        module.update(flags=flags, build_id=data[64:96].hex())
        for i in range(3):
            offset, address, size = struct.unpack_from("<III", data, 16 + 16*i)
            packed = struct.unpack_from("<I", data, 96 + 4*i)[0] if flags & (1 << i) else size
            segment = {"index": i, "offset": offset, "address": address,
                       "packed": packed, "decoded": size,
                       "hash_required": bool(flags & (1 << (i+3)))}
            module["segments"].append(segment)
            try:
                if size > 512*1024*1024 or offset < 256 or offset+packed > len(data):
                    raise ValueError("Invalid segment bounds; no extent recovery attempted")
                block = data[offset:offset+packed]
                if not flags & (1 << i):
                    decoded = block
                elif flags & 0x80:
                    if decoder is None:
                        raise ValueError("ZBIC decoder unavailable")
                    destination = ctypes.create_string_buffer(size)
                    if not decoder(destination, size, block, packed):
                        raise ValueError("ZBIC decoding failed")
                    decoded = destination.raw
                else:
                    decoded = lz4.block.decompress(block, uncompressed_size=size)
                segment["decoded_bytes"] = len(decoded)
                segment["hash_matches"] = hashlib.sha256(decoded).digest() == data[160+32*i:192+32*i]
                segment["ok"] = len(decoded) == size and (segment["hash_matches"] or not segment["hash_required"])
            except (ValueError, lz4.block.LZ4BlockError) as error:
                segment.update(ok=False, error=str(error))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exefs", nargs="+", type=Path)
    parser.add_argument("--zbic-library", type=Path)
    args = parser.parse_args()
    decoder = None
    if args.zbic_library:
        library = ctypes.CDLL(str(args.zbic_library.resolve()))
        decoder = library.nx2_zbic_decode
        decoder.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
        decoder.restype = ctypes.c_int
    print(json.dumps([audit(path, decoder) for path in args.exefs], indent=2))


if __name__ == "__main__":
    main()
