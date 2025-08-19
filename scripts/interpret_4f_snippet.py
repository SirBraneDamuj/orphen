#!/usr/bin/env python3
"""
Interpret a 0x4F opcode at a given offset and report the next opcode.

Opcode 0x4F semantics (from analysis of FUN_0025e7c0 — process_pending_spawn_requests):
- Consumes only the opcode byte (no immediate operands in the byte stream).
- Operates on a pre-populated global pending list (DAT_003556e8, count DAT_003556e4).
- For each entry, ensures the referenced model/descriptor is loaded and instantiates/initializes it.
- Therefore, in the script stream, the length of this opcode is 1 byte, and the next opcode
  begins immediately at offset+1.

This tool verifies the opcode at the given offset and prints the next opcode and its offset.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from typing import List, Dict, Any


def parse_0x4f_at(data: bytes, offset: int) -> Dict[str, Any]:
    if offset < 0 or offset >= len(data):
        raise ValueError(f"Offset 0x{offset:X} out of range for file of size {len(data)}")

    opcode = data[offset]
    ok = (opcode == 0x4F)

    # 0x4F consumes no immediate bytes
    end_offset = offset + 1
    next_opcode = data[end_offset] if end_offset < len(data) else None

    return {
        "offset": offset,
        "offset_hex": f"0x{offset:X}",
        "opcode_byte": opcode,
        "opcode_ok": ok,
        "length": 1,
        "end_offset": end_offset,
        "end_offset_hex": f"0x{end_offset:X}",
        "next_opcode": next_opcode,
        "next_opcode_hex": (None if next_opcode is None else f"0x{next_opcode:02X}"),
        "notes": [
            "0x4F reads no operands from the stream; it processes a global pending list",
            "The next opcode begins immediately after the 0x4F byte",
        ],
    }


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description="Interpret 0x4F snippet from a script blob and report next opcode")
    ap.add_argument("--file", default="scr2.out", help="Input script blob (default: scr2.out)")
    ap.add_argument("--offset", default="0x180E", help="Byte offset to parse (hex like 0x180E or decimal)")
    ap.add_argument("--json", action="store_true", help="Print JSON output only")
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
        result = parse_0x4f_at(data, offs)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(result, indent=2))
        return 0

    print(f"File: {args.file}")
    print(f"Offset: {result['offset_hex']} ({result['offset']})")
    print(f"Opcode byte: 0x{result['opcode_byte']:02X} (ok={result['opcode_ok']})")
    print(f"Length: {result['length']}")
    print(f"End offset: {result['end_offset_hex']} ({result['end_offset']})")
    if result["next_opcode"] is None:
        print("Next opcode: <EOF>")
    else:
        print(f"Next opcode @ {result['end_offset_hex']}: {result['next_opcode_hex']}")
    print("Notes:")
    for n in result["notes"]:
        print(f"  - {n}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
