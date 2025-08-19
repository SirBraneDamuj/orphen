#!/usr/bin/env python3
"""
Interpret a 0x4D opcode snippet from a script blob.

Opcode 0x4D semantics (from analysis of FUN_0025e628):
- Byte 0: opcode (0x4D)
- Byte 1: count N
- Next N entries: each a 32-bit little-endian value; the VM uses only the low 16 bits (stores as u16)
- The VM appends a 0 terminator in a scratch buffer (not present in the stream), then
    processes the list via FUN_002661f8 (skips negatives, handles non-negative IDs).
- Scratch arena allocation size (blocks of 16 bytes): ceil((N + 1)/8)

This tool parses bytes at a given offset and prints a summary of the decoded list.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from typing import List, Tuple


def parse_0x4d_at(data: bytes, offset: int) -> dict:
    if offset < 0 or offset >= len(data):
        raise ValueError(f"Offset 0x{offset:X} out of range for file of size {len(data)}")

    start = offset
    opcode = data[start]
    if opcode != 0x4D:
        # Don't abort; still try to parse as if it's 0x4D to aid investigation
        pass

    if start + 1 >= len(data):
        raise ValueError("Truncated: missing count byte after opcode")
    count = data[start + 1]

    ids_start = start + 2
    ids_bytes_len = count * 4
    ids_end = ids_start + ids_bytes_len
    if ids_end > len(data):
        raise ValueError(
            f"Truncated: need {ids_bytes_len} bytes for {count} IDs at 0x{ids_start:X},"
            f" file ends at 0x{len(data):X}"
        )

    ids_le_bytes = data[ids_start:ids_end]
    ids_u32: List[int] = []
    ids_u16_low: List[int] = []
    ids_s16_low: List[int] = []
    for i in range(0, len(ids_le_bytes), 4):
        val = (
            ids_le_bytes[i]
            | (ids_le_bytes[i + 1] << 8)
            | (ids_le_bytes[i + 2] << 16)
            | (ids_le_bytes[i + 3] << 24)
        )
        ids_u32.append(val)
        lo = val & 0xFFFF
        ids_u16_low.append(lo)
        ids_s16_low.append(lo - 0x10000 if lo & 0x8000 else lo)

    # Arena blocks as in FUN_0025e628: blocks = floor((count + 8) / 8) == ceil((count+1)/8)
    alloc_blocks = (count + 8) >> 3

    return {
        "offset": start,
        "offset_hex": f"0x{start:X}",
        "opcode_byte": opcode,
        "opcode_ok": opcode == 0x4D,
        "count": count,
    "ids_u32": ids_u32,
    "ids_u16_low": ids_u16_low,
    "ids_s16_low": ids_s16_low,
        "alloc_blocks_16B": alloc_blocks,
        "stream_span": {
            "start": ids_start,
            "start_hex": f"0x{ids_start:X}",
            "end": ids_end,
            "end_hex": f"0x{ids_end:X}",
            "length": ids_bytes_len,
        },
        "notes": [
            "IDs are 32-bit little-endian; VM uses only the low 16 bits",
            "A 0 terminator is appended in scratch memory (not present in stream)",
            "FUN_002661f8 skips negative IDs and processes non-negative IDs via FUN_002661a8",
        ],
    }


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description="Interpret 0x4D snippet from a script blob")
    ap.add_argument(
        "--file",
        default="scr2.out",
        help="Input script blob (default: scr2.out)",
    )
    ap.add_argument(
        "--offset",
        default="0x17D4",
        help="Byte offset to parse (hex like 0x17D4 or decimal)",
    )
    ap.add_argument(
        "--json",
        action="store_true",
        help="Print JSON output only",
    )
    args = ap.parse_args(argv)

    # Parse offset
    try:
        offs = int(args.offset, 0)
    except ValueError:
        print(f"Invalid --offset: {args.offset}", file=sys.stderr)
        return 2

    # Load file
    if not os.path.exists(args.file):
        print(f"Input not found: {args.file}", file=sys.stderr)
        return 2
    with open(args.file, "rb") as f:
        data = f.read()

    try:
        result = parse_0x4d_at(data, offs)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(result, indent=2))
        return 0

    # Human-readable summary
    print(f"File: {args.file}")
    print(f"Offset: {result['offset_hex']} ({result['offset']})")
    print(f"Opcode byte: 0x{result['opcode_byte']:02X} (ok={result['opcode_ok']})")
    print(f"Count: {result['count']}")
    print(f"IDs (u32 raw): {result['ids_u32']}")
    print(f"IDs (u16 low): {result['ids_u16_low']}")
    print(f"IDs (s16 low): {result['ids_s16_low']}")
    print(
        f"Stream span: {result['stream_span']['start_hex']}..{result['stream_span']['end_hex']} "
        f"({result['stream_span']['length']} bytes)"
    )
    print(f"Scratch alloc blocks (16B each): {result['alloc_blocks_16B']}")
    print("Notes:")
    for n in result["notes"]:
        print(f"  - {n}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
