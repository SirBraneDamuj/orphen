#!/usr/bin/env python3
"""
PSB4 offline parser (code-derived)

Parses a decoded PSB4 blob based on loader behavior in src/FUN_0022ce60.c.
This is intentionally conservative: it bounds-checks and reports counts/strides
without guessing semantics beyond what code shows.

Sections (relative to PSB4 base):
- +0x00: magic 'PSB4' (0x34425350)
- +0x04: offs_sectionA
- +0x08: offs_sectionB
- +0x0C: offs_sectionC

Section A
- +0x00: s16 countA
- For i in 0..countA-1: the code reads dwords at entry+0x04 and entry+0x0C,
  and one dword from a parallel region stepping by 3 dwords per entry.
  We expose countA and compute a conservative data span.

Section B
- +0x00: s16 countB
- For each record, input consumption is 24 bytes: 2 (flags) + 8 (4x u16) + 12 (4x 3 bytes)

Section C
- +0x00: s16 groupCount
- For each group: s16 n, then n*3 s16s

Usage:
  python scripts/psb4_parser.py path/to/file --offset 0x...
"""
from __future__ import annotations
import argparse
import json
import struct
from dataclasses import dataclass, asdict
from typing import List, Tuple, Any

MAGIC_PSB4 = 0x34425350
LE32 = '<I'
LE16 = '<H'
LE16S = '<h'

@dataclass
class SectionAInfo:
    count: int
    span_bytes: int

@dataclass
class SectionBInfo:
    count: int
    stride_in_bytes: int = 24

@dataclass
class Group:
    tri_count: int
    indices: List[int]

@dataclass
class SectionCInfo:
    group_count: int
    groups: List[Group]

@dataclass
class PSB4Summary:
    magic: int
    offs_a: int
    offs_b: int
    offs_c: int
    section_a: SectionAInfo | None
    section_b: SectionBInfo | None
    section_c: SectionCInfo | None

    def to_json(self) -> str:
        return json.dumps(asdict(self), indent=2)


def _safe_slice(buf: bytes, off: int, size: int) -> bytes:
    end = off + size
    if off < 0 or size < 0 or end > len(buf):
        raise ValueError(f'slice OOB off={off} size={size} len={len(buf)}')
    return buf[off:end]


def parse_psb4(buf: bytes) -> PSB4Summary:
    if len(buf) < 0x10:
        raise ValueError('buffer too small for header')
    magic, = struct.unpack_from(LE32, buf, 0)
    if magic != MAGIC_PSB4:
        raise ValueError(f'bad magic 0x{magic:08X}')
    offs_a, = struct.unpack_from(LE32, buf, 0x04)
    offs_b, = struct.unpack_from(LE32, buf, 0x08)
    offs_c, = struct.unpack_from(LE32, buf, 0x0C)

    sec_a: SectionAInfo | None = None
    if offs_a:
        a0 = offs_a
        if a0 + 2 <= len(buf):
            count_a, = struct.unpack_from(LE16S, buf, a0)
            # minimum span to cover entry header and the referenced dwords
            # We conservatively assume entry size of at least 0x10
            span = 2 + max(0, count_a) * 0x10
            sec_a = SectionAInfo(count=count_a, span_bytes=min(span, max(0, len(buf)-a0)))

    sec_b: SectionBInfo | None = None
    if offs_b:
        b0 = offs_b
        if b0 + 2 <= len(buf):
            count_b, = struct.unpack_from(LE16S, buf, b0)
            sec_b = SectionBInfo(count=count_b)

    sec_c: SectionCInfo | None = None
    if offs_c:
        c0 = offs_c
        if c0 + 2 <= len(buf):
            group_count, = struct.unpack_from(LE16S, buf, c0)
            groups: List[Group] = []
            p = c0 + 2
            for _ in range(max(0, group_count)):
                if p + 2 > len(buf):
                    break
                n, = struct.unpack_from(LE16S, buf, p)
                p += 2
                tri_cnt = max(0, n)
                need = tri_cnt * 3 * 2
                if p + need > len(buf):
                    break
                idx = list(struct.unpack_from('<' + 'h' * (tri_cnt * 3), buf, p))
                p += need
                groups.append(Group(tri_count=tri_cnt, indices=[int(x) for x in idx]))
            sec_c = SectionCInfo(group_count=group_count, groups=groups)

    return PSB4Summary(
        magic=magic,
        offs_a=offs_a,
        offs_b=offs_b,
        offs_c=offs_c,
        section_a=sec_a,
        section_b=sec_b,
        section_c=sec_c,
    )


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description='PSB4 parser (code-derived)')
    ap.add_argument('input', help='path to decoded PSB4-containing file')
    ap.add_argument('--offset', type=lambda s: int(s, 0), default=0, help='offset of PSB4 (decimal or 0x..)')
    ap.add_argument('--pretty', action='store_true')
    args = ap.parse_args(argv)
    with open(args.input, 'rb') as f:
        data = f.read()
    if args.offset:
        if args.offset < 0 or args.offset >= len(data):
            raise SystemExit(f'offset {args.offset} out of range (len={len(data)})')
        data = data[args.offset:]
    try:
        summary = parse_psb4(data)
    except Exception as e:
        print(json.dumps({'error': str(e)}))
        return 1
    if args.pretty:
        print(summary.to_json())
    else:
        print(json.dumps(json.loads(summary.to_json()), separators=(',', ':')))
    return 0

if __name__ == '__main__':
    import sys
    raise SystemExit(main(sys.argv[1:]))
