#!/usr/bin/env python3
"""
hexdump_range.py

Simple helper to dump one or more address ranges from a flat EE memory image (eeMemory.bin).
Assumes the file is a raw dump where file offset == EE address unless a --base offset is given.

Usage:
  python hexdump_range.py -f eeMemory.bin 0x34ff70:0x350030 0x350e00:0x351040

Range syntax:
  start:end  (end is exclusive)  Accepts hex (0x...) or decimal. You can also specify +length as start+len:
  start+len  (len bytes from start)

Options:
  -f / --file   Path to memory image (required)
  --base HEX    Base address that corresponds to file offset 0 (default 0)
  --out PATH    Write output to a file instead of stdout
  --width N     Bytes per line (default 16)
  --no-ascii    Suppress ASCII column

Example:
  python hexdump_range.py -f eeMemory.bin 0x34fff0+0x40

Exit codes:
  0 success, 1 usage error, 2 I/O error.
"""
import argparse
import os
import sys
from typing import Iterable, Tuple


def parse_int(v: str) -> int:
    v = v.strip()
    base = 16 if v.lower().startswith("0x") else 10
    return int(v, base)


def parse_range(r: str) -> Tuple[int, int]:
    if ':' in r:
        a, b = r.split(':', 1)
        start = parse_int(a)
        end = parse_int(b)
        if end < start:
            raise ValueError(f"end < start in range '{r}'")
        return start, end
    if '+' in r:
        a, l = r.split('+', 1)
        start = parse_int(a)
        length = parse_int(l)
        if length < 0:
            raise ValueError(f"negative length in range '{r}'")
        return start, start + length
    raise ValueError(f"Unrecognized range syntax: {r}")


def hexdump(data: bytes, start_addr: int, width: int, show_ascii: bool, out) -> None:
    for offset in range(0, len(data), width):
        chunk = data[offset:offset+width]
        addr = start_addr + offset
        hex_bytes = ' '.join(f"{b:02x}" for b in chunk)
        if len(chunk) < width:
            # pad spacing so ASCII column lines up
            hex_bytes += '   ' * (width - len(chunk))
        if show_ascii:
            ascii_repr = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
            out.write(f"{addr:08x}: {hex_bytes}  |{ascii_repr}|\n")
        else:
            out.write(f"{addr:08x}: {hex_bytes}\n")


def main(argv: Iterable[str]) -> int:
    ap = argparse.ArgumentParser(description="Hexdump one or more address ranges from a flat memory image")
    ap.add_argument('-f', '--file', required=True, help='Path to memory image (e.g., eeMemory.bin)')
    ap.add_argument('--base', default='0', help='Base address corresponding to file offset 0 (hex or dec)')
    ap.add_argument('--out', help='Output file (default stdout)')
    ap.add_argument('--width', type=int, default=16, help='Bytes per line (default 16)')
    ap.add_argument('--no-ascii', action='store_true', help='Suppress ASCII column')
    ap.add_argument('ranges', nargs='+', help='Address ranges start:end or start+len')
    args = ap.parse_args(list(argv))

    try:
        base = parse_int(args.base)
    except Exception as e:
        print(f"Error parsing base: {e}", file=sys.stderr)
        return 1

    try:
        ranges = [parse_range(r) for r in args.ranges]
    except Exception as e:
        print(f"Error parsing range: {e}", file=sys.stderr)
        return 1

    path = args.file
    try:
        size = os.path.getsize(path)
    except OSError as e:
        print(f"I/O error accessing file: {e}", file=sys.stderr)
        return 2

    with open(path, 'rb') as f, (open(args.out, 'w') if args.out else sys.stdout) as out:
        for (start, end) in ranges:
            if end == start:
                continue
            out.write(f"# Range {start:08x}-{end:08x} (len {end-start} bytes)\n")
            # translate to file offsets
            off_start = start - base
            off_end = end - base
            if off_start < 0 or off_end < 0:
                out.write(f"# Skipping range below base (start={start:08x})\n")
                continue
            if off_start >= size:
                out.write(f"# Skipping range beyond file (start offset {off_start} >= size {size})\n")
                continue
            if off_end > size:
                off_end = size
                out.write(f"# Truncated end to file size {size}\n")
            f.seek(off_start)
            data = f.read(off_end - off_start)
            hexdump(data, start, args.width, not args.no_ascii, out)
            out.write('\n')
    return 0


if __name__ == '__main__':  # pragma: no cover
    raise SystemExit(main(sys.argv[1:]))
