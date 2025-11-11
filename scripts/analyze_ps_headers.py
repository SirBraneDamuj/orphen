#!/usr/bin/env python3
"""
Quickly inspect PS* headers (PSM2 / PSC3 / PSB4) to compare common layout.

For each file, dump:
- first 64 bytes hex
- dwords at offsets 0x00..0x40 step 4 (little-endian)
- interpret offsets at 0x04/0x08/0x0C/0x10/... as potential section offsets

Supports scanning a directory like out/maps and filtering by magic (decoded-first).
"""
from __future__ import annotations

import argparse
import os
import sys
from typing import Iterable

MAGICS = {b"PSM2", b"PSC3", b"PSB4"}


def read(path: str, n: int) -> bytes:
    with open(path, 'rb') as f:
        return f.read(n)


def dwords_le(buf: bytes) -> list[int]:
    out = []
    for i in range(0, min(len(buf), 0x44), 4):
        if i + 4 <= len(buf):
            out.append(int.from_bytes(buf[i:i+4], 'little'))
    return out


def classify_from_bytes(b: bytes) -> str:
    m = b[:4].upper()
    try:
        return m.decode('ascii') if m in MAGICS else 'OTHER'
    except Exception:
        return 'OTHER'


def scan_dir(dir_path: str, include: set[str] | None, limit: int | None) -> Iterable[str]:
    files = sorted(os.listdir(dir_path))
    count = 0
    for name in files:
        if not (name.endswith('.psm2') or name.endswith('.dec.bin') or name.endswith('.bin')):
            continue
        p = os.path.join(dir_path, name)
        b = read(p, 4)
        magic = classify_from_bytes(b)
        if include and magic not in include:
            continue
        yield p
        count += 1
        if limit and count >= limit:
            break


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description='Analyze PS* headers')
    ap.add_argument('path', nargs='?', default='out/maps', help='File or directory to analyze')
    ap.add_argument('--only', choices=['PSM2','PSC3','PSB4','OTHER'], help='Filter by magic (raw header)')
    ap.add_argument('--limit', type=int, default=10, help='Limit number of files when scanning a directory')
    args = ap.parse_args(argv)

    paths: list[str]
    if os.path.isdir(args.path):
        incl = {args.only} if args.only else None
        paths = list(scan_dir(args.path, incl, args.limit))
    else:
        paths = [args.path]

    for p in paths:
        b = read(p, 0x80)
        magic = classify_from_bytes(b)
        print(f"== {p}")
        print(f"magic: {magic}")
        # hex dump first 64
        print(b[:64].hex(' ', 1))
        # dwords
        dw = dwords_le(b)
        print('dwords@0x00..0x40 (le):', ' '.join(f"{x:08x}" for x in dw))
        # potential offsets from 0x04 etc
        if len(dw) >= 16:
            offs = dw[1:12]
            print('potential section offsets (0x04..0x2c):', [f"0x{x:08x}" for x in offs])
        print()

    return 0

if __name__ == '__main__':
    raise SystemExit(main())
