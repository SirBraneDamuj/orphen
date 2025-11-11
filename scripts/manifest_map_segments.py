#!/usr/bin/env python3
"""
Create a CSV manifest of MAP.BIN segments with header fields for analysis.
Columns:
 index, magic, d0..d15 (first 16 dwords, little-endian)

It uses extracted files in out/maps (prefers .psm2, else .dec.bin, else .bin).
"""
from __future__ import annotations

import csv
import os
import re
import sys
from typing import Optional

OUT_DIR = 'out/maps'


def read(path: str, n: int) -> bytes:
    with open(path, 'rb') as f:
        return f.read(n)


def dwords_le(buf: bytes, count: int = 16) -> list[int]:
    out = []
    for i in range(0, min(len(buf), count*4), 4):
        out.append(int.from_bytes(buf[i:i+4], 'little'))
    return out


def magic_str(b: bytes) -> str:
    try:
        m = b[:4].upper().decode('ascii')
        return m
    except Exception:
        return ''


def pick_file(idx: str, files: dict[str,str]) -> Optional[str]:
    # prefer decoded PSM2, then decoded, then raw
    if 'psm2' in files:
        return files['psm2']
    if 'dec.bin' in files:
        return files['dec.bin']
    return files.get('bin')


def main(argv=None) -> int:
    out_csv = argv[0] if argv else 'out/map_manifest.csv'
    os.makedirs(os.path.dirname(out_csv), exist_ok=True)

    rx = re.compile(r"^map_(\d{4})\.(psm2|dec\.bin|bin)$", re.IGNORECASE)
    by_idx: dict[str, dict[str,str]] = {}
    for name in os.listdir(OUT_DIR):
        m = rx.match(name)
        if not m:
            continue
        idx, ext = m.group(1), m.group(2).lower()
        d = by_idx.setdefault(idx, {})
        d[ext] = os.path.join(OUT_DIR, name)

    with open(out_csv, 'w', newline='') as f:
        w = csv.writer(f)
        header = ['index','magic'] + [f'd{i}' for i in range(16)]
        w.writerow(header)
        for idx in sorted(by_idx.keys(), key=lambda s: int(s)):
            p = pick_file(idx, by_idx[idx])
            if not p:
                continue
            b = read(p, 64)
            magic = magic_str(b)
            dws = dwords_le(b, 16)
            row = [idx, magic] + [f"0x{x:08x}" for x in dws + [0]*(16-len(dws))]
            w.writerow(row)

    print(f"Wrote {out_csv}")
    return 0

if __name__ == '__main__':
    import sys
    raise SystemExit(main(sys.argv[1:]))
