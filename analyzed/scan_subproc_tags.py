#!/usr/bin/env python3
"""
Scan scr2.out for suspected SUBPROC tags based on runtime evidence:
- Raw pattern: 0x0B 0x04 <ID16 little-endian>
- Prologue after 0x32 matching the same pattern (payload = 4 bytes)
Print offsets, IDs, and a short context.
"""
from __future__ import annotations
import argparse
from pathlib import Path

def scan(data: bytes, start: int = 0, limit: int | None = None):
    limit = limit if limit is not None else len(data)
    if limit > len(data):
        limit = len(data)
    hits = []
    # Raw 0x0B 0x04 <ID16>
    for off in range(start, max(start, limit - 3)):
        if data[off] == 0x0B and data[off+1] == 0x04:
            id16 = data[off+2] | (data[off+3] << 8)
            ctx = data[max(0, off-8):min(len(data), off+8)].hex(" ")
            hits.append((off, id16, "RAW", ctx))
    # Prologue 0x32 + 4-byte payload
    for off in range(start, max(start, limit - 5)):
        if data[off] == 0x32:
            pl = data[off+1:off+5]
            if len(pl) == 4 and pl[0] == 0x0B and pl[1] == 0x04:
                id16 = pl[2] | (pl[3] << 8)
                ctx = data[max(0, off-8):min(len(data), off+8)].hex(" ")
                hits.append((off, id16, "PROLOGUE", ctx))
    hits.sort()
    return hits


def main():
    ap = argparse.ArgumentParser(description="Scan for suspected SUBPROC tags")
    ap.add_argument("file", type=Path)
    ap.add_argument("--limit", type=lambda x: int(x, 0), default=None)
    args = ap.parse_args()

    data = args.file.read_bytes()
    hits = scan(data, 0, args.limit)
    for off, sid, kind, ctx in hits:
        print(f"{off:08x} {kind:9} ID16={sid} (0x{sid:04x})  ctx: {ctx}")

if __name__ == "__main__":
    main()
