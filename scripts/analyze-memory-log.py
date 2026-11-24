#!/usr/bin/env python3
"""Offline read-only analysis of existing BackupLoaderMKW diagnostic logs.
No console interaction; never infer a fault merely from the last log line.
"""
import argparse
import collections
import re
from pathlib import Path

def fields(line):
    return dict(re.findall(r"(\w+)=([^\s]+)", line))

def num(d,k,base=10):
    try:
        return int(d.get(k,""),base)
    except ValueError:
        return None

def analyse(crash, primary):
    pairs = {}
    completed = collections.Counter()
    errors = []
    active = []
    overlap = []
    unmatched_unmaps = []
    faults = []
    accounting = []
    gpu = []
    gaps = []
    for line in crash.splitlines():
        d = fields(line)
        if line.startswith("SHARED_MEMORY_EVENT "):
            ident = num(d,"id")
            phase = num(d,"phase")
            op = num(d,"op")
            if phase == 1:
                pairs[ident] = d
            elif phase == 2:
                begin = pairs.pop(ident,None)
                rc = num(d,"result",16)
                completed[(op,rc)] += 1
                if rc != 0:
                    errors.append(line)
                    continue
                addr = num(d,"address",16)
                size = num(d,"bytes")
                if addr is None or size is None or size <= 0 or op not in (1,2,4,5):
                    continue
                end = addr + size
                if op in (1,4):
                    hits = [x for x in active if x[0]<end and addr<x[1]]
                    if hits:
                        overlap.append((ident,addr,end,[x[2] for x in hits]))
                    active.append((addr,end,ident))
                else:
                    hits = [x for x in active if x[0]<end and addr<x[1]]
                    if not hits:
                        unmatched_unmaps.append(ident)
                    updated = []
                    for lo,hi,prior in active:
                        if lo>=end or hi<=addr:
                            updated.append((lo,hi,prior))
                        else:
                            if lo<addr: updated.append((lo,addr,prior))
                            if hi>end: updated.append((end,hi,prior))
                    active = updated
        elif line.startswith("NATIVE_CPU_FAULT "):
            faults.append(d)
        elif line.startswith(("PHYSICAL_ACCOUNTING_CHECK ","PROCESS_PHYSICAL_SNAPSHOT ","ACCOUNTING_OBSERVATION ")):
            accounting.append(line)
        elif line.startswith(("GPU_INIT_SNAPSHOT ","GPU_BOUNDARY_SNAPSHOT ","GPU_OBSERVATION_MODE ","NVN_LAUNCH_MODE ")):
            gpu.append(line)
        elif "HISTORY_GAP" in line:
            gaps.append(line)
    print("MEMORY OPERATIONS (observed returns)")
    for (op,rc),n in sorted(completed.items(),key=str):
        print(f"op={op} result={rc:08x} count={n}" if rc is not None else f"op={op} result=unknown count={n}")
    print("Unmatched entries:",len(pairs),"Failed returns:",len(errors))
    for line in errors[-20:]: print(line)
    print("Potential overlapping observed map ranges:",len(overlap))
    for item in overlap[-10:]: print(item)
    print("Unmaps without observed prior map:",len(unmatched_unmaps))
    print("History gaps:",len(gaps))
    print("Range ledger is approximate: missing events, concurrent ordering and unobserved mappings limit conclusions.")
    print("\nACCOUNTING (last observations)")
    for line in accounting[-9:]: print(line)
    for line in primary.splitlines():
        if "GUEST_MEMORY_ACCOUNTING_CAPPED" in line: print(line)
    print("\nGPU (last observations)")
    for line in gpu[-12:]: print(line)
    print("Zero counters in full-native mode do not establish absence of GPU activity.")
    print("\nCPU FAULTS")
    if not faults: print("No captured CPU exception; cause remains unknown.")
    for d in faults:
        far=num(d,"far",16)
        hits=[x for x in active if far is not None and x[0]<=far<x[1]]
        print(d)
        print("Fault address in final observed range ledger:",hits)
        print("Final ledger is not necessarily the mapping state at fault time.")

if __name__ == "__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("crash",type=Path)
    p.add_argument("primary",type=Path,nargs="?")
    args=p.parse_args()
    analyse(args.crash.read_text(errors="replace"),args.primary.read_text(errors="replace") if args.primary else "")
