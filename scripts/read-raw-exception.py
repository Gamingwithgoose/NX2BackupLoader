"""Read the launcher's raw exception sidecar without modifying it."""
import argparse
import json
import struct
from pathlib import Path


def decode(data):
    if len(data) != 512 or data[:8] != b"NX2RAW02" or data[504:] != b"RAW2END!":
        raise ValueError("Not a complete raw-exception v2 file")
    version, captured = struct.unpack_from("<II", data, 8)
    size, checksum = struct.unpack_from("<II", data, 32)
    if version != 2 or size != 336 or captured not in (0, 1):
        raise ValueError("Unsupported raw-exception header")
    host, guest = struct.unpack_from("<QQ", data, 16)
    result = {"version": version, "captured": bool(captured),
              "host": f"{host:016x}", "guest": f"{guest:016x}"}
    if not captured:
        result["meaning"] = "File was prepared; no persisted raw exception is present"
        return result
    raw = data[40:376]
    computed = 2166136261
    for byte in raw:
        computed = ((computed ^ byte) * 16777619) & 0xffffffff
    if computed != checksum:
        raise ValueError("Raw exception checksum failed; do not interpret partial data")
    state, valid = struct.unpack_from("<II", raw)
    if state not in (2, 3) or valid not in (0, 1):
        raise ValueError("Raw exception snapshot was not published")
    kind, context, entry_sp, tick = struct.unpack_from("<4Q", raw, 8)
    frame = struct.unpack_from("<15Q", raw, 40)
    gprs = struct.unpack_from("<21Q", raw, 160)
    result.update(kind=f"{kind:x}", context_valid=bool(valid),
                  context=f"{context:016x}", entry_sp=f"{entry_sp:016x}", tick=tick)
    result["registers"] = {f"x{i+9}": f"{v:016x}" for i, v in enumerate(gprs)}
    if valid:
        result.update(pc=f"{frame[11]:016x}", lr=f"{frame[9]:016x}",
                      sp=f"{frame[10]:016x}", far=f"{frame[14]:016x}",
                      esr=f"{frame[13] >> 32:08x}", pstate=f"{frame[12] & 0xffffffff:08x}")
        result["registers"].update({f"x{i}": f"{v:016x}" for i, v in enumerate(frame[:9])})
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(decode(args.path.read_bytes()), indent=2))
    except (ValueError, OSError) as error:
        parser.exit(1, f"{error}\n")
