"""Bounded, read-only NPDM metadata inspection. Does not validate RSA signatures."""
import hashlib
import struct
from pathlib import Path


def parse_policy(data):
    if len(data) < 0x80 or len(data) > 0x10000 or data[:4] != b"META":
        raise ValueError("Missing complete NPDM META header or file exceeds 64 KiB")
    def word(offset):
        return struct.unpack_from("<I", data, offset)[0]
    def region(offset, size, minimum=0):
        if size < minimum or offset < 0x80 or offset > len(data) or size > len(data)-offset:
            raise ValueError("NPDM section outside file or too short")
        return data[offset:offset+size]
    aci_offset, aci_size, acid_offset, acid_size = struct.unpack_from("<IIII", data, 0x70)
    aci = region(aci_offset, aci_size, 0x40)
    acid = region(acid_offset, acid_size, 0x40)
    if aci_offset < acid_offset + acid_size and acid_offset < aci_offset + aci_size:
        raise ValueError("Overlapping NPDM ACI0/ACID sections")
    if aci[:4] != b"ACI0":
        raise ValueError("ACI0 magic missing at declared offset")
    platform = next((name for start, name in ((0x300, "Switch2"), (0x200, "Switch1"))
                     if acid[start:start+4] == b"ACID"), None)
    if platform is None:
        raise ValueError("ACID header missing after supported signature/modulus layout")
    def section(pair_offset):
        offset, size = struct.unpack_from("<II", aci, pair_offset)
        if not size:
            return b""
        if offset < 0x40 or offset > len(aci) or size > len(aci)-offset:
            raise ValueError("ACI0 access-control section outside ACI0")
        return aci[offset:offset+size]
    fs, services, caps = section(0x20), section(0x28), section(0x30)
    accessed, hosted = [], []
    cursor = 0
    while cursor < len(services):
        control = services[cursor]
        size = (control & 0xF) + 1
        cursor += 1
        if size > len(services) - cursor:
            raise ValueError("Truncated service access-control entry")
        name = services[cursor:cursor+size].decode("ascii")
        if any(ord(c) < 0x20 or ord(c) > 0x7e for c in name):
            raise ValueError("Non-printable service access-control entry")
        (hosted if control & 0x80 else accessed).append(name)
        cursor += size
    if len(caps) % 4:
        raise ValueError("Kernel capability section is not word-aligned")
    svcs = set()
    descriptors = list(struct.unpack("<" + "I"*(len(caps)//4), caps))
    decoded = {}
    for descriptor in descriptors:
        if descriptor == 0xffffffff:
            continue
        kind, value = 0, descriptor
        while value & 1:
            kind += 1
            value >>= 1
        value >>= 1
        if kind == 4:
            group = value >> 24
            svcs.update(group*24+i for i in range(24) if value & (1 << i))
        elif kind == 3:
            priorities = (value & 63, (value >> 6) & 63)
            decoded["thread_cpu_flags"] = {"priority_min_numeric": min(priorities),
                "priority_max_numeric": max(priorities),
                "cpu_min": (value >> 12) & 255, "cpu_max": (value >> 20) & 255}
        elif kind == 13:
            decoded["application_type"] = value & 7
        elif kind == 14:
            decoded["minimum_kernel_version_raw"] = value
        elif kind == 15:
            decoded["handle_table_size"] = value
    return {"sha256": hashlib.sha256(data).hexdigest(), "platform": platform,
        "program_id": f"{struct.unpack_from('<Q', aci, 0x10)[0]:016x}",
        "mmu_flags": data[0xC], "main_thread_priority": data[0xE], "default_cpu": data[0xF],
        "main_stack_bytes": word(0x1C), "service_access": sorted(set(accessed)),
        "service_host": sorted(set(hosted)), "declared_syscalls": sorted(svcs),
        "kernel_descriptors": [f"{v:08x}" for v in descriptors], "decoded_capabilities": decoded,
        "filesystem_permissions_raw": f"{struct.unpack_from('<Q',fs,4)[0]:016x}" if len(fs)>=12 else None,
        "signature_verified": False}


def read_policy(path):
    path = Path(path)
    with path.open("rb") as stream:
        data = stream.read(0x10001)
    result = parse_policy(data)
    result["file"] = str(path.resolve())
    return result


def service_covered(name, patterns):
    return any(name == p or (p.endswith("*") and name.startswith(p[:-1])) for p in patterns)


def compare_policies(host, target):
    fields = ("platform", "mmu_flags", "main_thread_priority", "default_cpu", "main_stack_bytes", "filesystem_permissions_raw", "decoded_capabilities")
    return {"launchability": "UNDETERMINED", "host": host, "target": target,
        "target_services_not_declared_by_host": [s for s in target["service_access"] if not service_covered(s, host["service_access"])],
        "target_syscalls_not_declared_by_host": sorted(set(target["declared_syscalls"]) - set(host["declared_syscalls"])),
        "different_fields": {f: {"host": host[f], "target": target[f]} for f in fields if host[f] != target[f]},
        "limitations": ["Compares ACI0 declarations from PC files, not effective permissions of a running process.",
            "ACID signatures and all capability semantics are not verified; matching declarations do not prove compatibility.",
            "HEN service overrides can differ from these declarations; an unmodified game does not automatically use libounce overrides.",
            "No process launch, ticket change, identity spoofing, save access, or console connection occurs."]}
