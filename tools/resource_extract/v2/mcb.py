#!/usr/bin/env python3
"""MCB (Master Chunk Bundle) extractor.

Code-derived from:
- src/FUN_00222638.c (MCB0 loader — reads 12,000 bytes into DAT_00315b00)
- src/FUN_00222898.c (per-section loader — byte_offset/byte_size pair per slot)
- src/FUN_00222a40.c (per-section writer — same layout)

Layout:
  MCB0.BIN : 12,000 bytes = 15 sections × 100 entries × 8 bytes
             per entry (little-endian):
               u32 byte_offset    (absolute offset into MCB1.BIN)
               u32 byte_size      (0 = empty slot)
             indexed as  section * 800 + entry * 8

  MCB1.BIN : ~295 MB raw section blobs concatenated; NOT LZ-compressed at
             this level (the loader reads the bytes straight into RAM).

This tool dumps every non-empty slot to a file named `s{section:02d}_e{entry:03d}.bin`
and prints a summary with the magic of each chunk so we can tell the formats
apart.
"""
from __future__ import annotations

import argparse
import os
import struct
from collections import Counter
from typing import List, Tuple

N_SECTIONS = 15
N_ENTRIES = 100
MCB0_SIZE = N_SECTIONS * N_ENTRIES * 8  # 12000


def load_mcb0(path: str) -> List[List[Tuple[int, int]]]:
    """Return [section][entry] -> (offset, size); 0-size slots kept as-is."""
    data = open(path, 'rb').read()
    if len(data) != MCB0_SIZE:
        raise ValueError(f"expected MCB0.BIN of {MCB0_SIZE} bytes, got {len(data)}")
    sections: List[List[Tuple[int, int]]] = []
    for s in range(N_SECTIONS):
        row: List[Tuple[int, int]] = []
        for e in range(N_ENTRIES):
            off, size = struct.unpack_from('<II', data, s * 800 + e * 8)
            row.append((off, size))
        sections.append(row)
    return sections


def extract(mcb0_path: str, mcb1_path: str, dst_dir: str,
            verbose: bool = False) -> Tuple[int, Counter]:
    sections = load_mcb0(mcb0_path)
    os.makedirs(dst_dir, exist_ok=True)
    magics: Counter = Counter()
    written = 0
    mcb1_size = os.path.getsize(mcb1_path)
    with open(mcb1_path, 'rb') as fh:
        for s_idx, section in enumerate(sections):
            for e_idx, (off, size) in enumerate(section):
                if size == 0:
                    continue
                if off + size > mcb1_size:
                    if verbose:
                        print(f"[warn] s{s_idx:02d}_e{e_idx:03d}: "
                              f"range {off:#x}+{size} exceeds MCB1 size")
                    continue
                fh.seek(off)
                blob = fh.read(size)
                out_name = f"s{s_idx:02d}_e{e_idx:03d}.bin"
                with open(os.path.join(dst_dir, out_name), 'wb') as out:
                    out.write(blob)
                written += 1
                # Capture 4-byte magic for the summary
                m4 = blob[:4] if len(blob) >= 4 else blob.ljust(4, b'\x00')
                ascii4 = ''.join(chr(b) if 32 <= b < 127 else '.' for b in m4)
                magics[(m4.hex(), ascii4)] += 1
                if verbose:
                    print(f"[ok]   s{s_idx:02d}_e{e_idx:03d}: "
                          f"off={off:#010x} size={size:>10}  magic={ascii4!r}")
    return written, magics


def main(argv: List[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="MCB0/MCB1 section extractor"
    )
    ap.add_argument('--mcb0', default='MCB0.BIN', help='path to MCB0.BIN')
    ap.add_argument('--mcb1', default='MCB1.BIN', help='path to MCB1.BIN')
    ap.add_argument('--dst', default='out/mcb', help='output directory')
    ap.add_argument('--verbose', action='store_true')
    args = ap.parse_args(argv)

    written, magics = extract(args.mcb0, args.mcb1, args.dst,
                              verbose=args.verbose)
    print(f"\nExtracted {written} non-empty chunks into {args.dst}")
    print(f"\nTop 30 chunk magics (first 4 bytes):")
    for (hx, ascii4), count in magics.most_common(30):
        print(f"  {count:>5}  0x{hx}  {ascii4!r}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
