#!/usr/bin/env python3
"""
Extract segments from MAP.BIN and optionally LZ-decompress to PSM2.

- MAP.BIN format per analyzed/map_bin_format.h:
  u32 entry_count; then entry_count * u32 entries where
    raw_entry bits: size_in_words (17 LSB), sector_offset (15 MSB)
    byte_offset = sector_offset * 2048
    byte_size   = size_in_words * 4

- Each segment should be a packed PSM2 map chunk, sometimes compressed with the
  headerless LZ used elsewhere. We'll try to detect:
  1) If segment begins with b'PSM2' → write as raw
  2) Else, attempt headerless LZ decode and check for b'PSM2' magic
     → if yes, write decoded
     → else, write raw as fallback

Outputs:
  out_dir/
    map_{index:04}.bin         (raw segment as-is)
    map_{index:04}.psm2        (if segment appears to be PSM2, raw or decoded)
    map_{index:04}.dec.bin     (decoded bytes when decode produced non-PSM2 but changed size)

Prints a summary at the end.
"""

from __future__ import annotations

import argparse
import os
import struct
from typing import Tuple

# Local import of provided decoder
try:
    from orphen_lz_headerless_decoder import decode_bytes, calculate_decompressed_size
except Exception as e:
    decode_bytes = None  # type: ignore
    calculate_decompressed_size = None  # type: ignore

SECTOR_SIZE = 2048
MAGIC = b"PSM2"


def parse_entry(raw: int) -> Tuple[int, int]:
    size_words = raw & 0x1FFFF  # 17 bits
    sector_off = (raw >> 17) & 0x7FFF  # 15 bits
    return sector_off, size_words * 4


def try_decode(segment: bytes) -> Tuple[bytes, str]:
    """Attempt LZ decode; return (best_bytes, tag) where tag indicates detection.
    Tags: 'raw-psm2', 'decoded-psm2', 'decoded-diff', 'raw-unknown'
    """
    if segment.startswith(MAGIC):
        return segment, 'raw-psm2'
    if decode_bytes is None:
        return segment, 'raw-unknown'
    try:
        # Conservative: allow decoder to run; if output starts with PSM2 → good
        dec = decode_bytes(segment)
        if dec.startswith(MAGIC):
            return dec, 'decoded-psm2'
        # If decode changed size materially, keep it as an alternate artifact
        if dec and len(dec) != len(segment):
            return dec, 'decoded-diff'
    except Exception:
        pass
    return segment, 'raw-unknown'


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Extract MAP.BIN segments and try PSM2 decode")
    ap.add_argument("map_bin", help="Path to MAP.BIN")
    ap.add_argument("out_dir", help="Output directory")
    ap.add_argument("--limit", type=int, default=None, help="Limit number of entries to extract")
    args = ap.parse_args(argv)

    os.makedirs(args.out_dir, exist_ok=True)

    with open(args.map_bin, 'rb') as f:
        hdr = f.read(4)
        if len(hdr) < 4:
            raise SystemExit("MAP.BIN too small to contain header")
        (entry_count,) = struct.unpack('<I', hdr)
        entries = []
        for i in range(entry_count):
            raw_b = f.read(4)
            if len(raw_b) < 4:
                break
            (raw_entry,) = struct.unpack('<I', raw_b)
            entries.append(raw_entry)

        # Summary counters
        c_raw_psm2 = c_dec_psm2 = c_dec_diff = c_raw_unknown = 0

        for idx, raw in enumerate(entries):
            if args.limit is not None and idx >= args.limit:
                break
            sector_off, size = parse_entry(raw)
            if size == 0:
                continue
            byte_off = sector_off * SECTOR_SIZE
            f.seek(byte_off)
            data = f.read(size)
            # Write raw dump
            raw_path = os.path.join(args.out_dir, f"map_{idx:04}.bin")
            with open(raw_path, 'wb') as wf:
                wf.write(data)
            # Try decode
            best, tag = try_decode(data)
            if tag == 'raw-psm2':
                c_raw_psm2 += 1
                p = os.path.join(args.out_dir, f"map_{idx:04}.psm2")
                with open(p, 'wb') as wf:
                    wf.write(best)
            elif tag == 'decoded-psm2':
                c_dec_psm2 += 1
                p = os.path.join(args.out_dir, f"map_{idx:04}.psm2")
                with open(p, 'wb') as wf:
                    wf.write(best)
            elif tag == 'decoded-diff':
                c_dec_diff += 1
                p = os.path.join(args.out_dir, f"map_{idx:04}.dec.bin")
                with open(p, 'wb') as wf:
                    wf.write(best)
            else:
                c_raw_unknown += 1

        print(f"Entries read: {len(entries)}")
        print(f"Raw PSM2:    {c_raw_psm2}")
        print(f"Decoded PSM2:{c_dec_psm2}")
        print(f"Decoded diff:{c_dec_diff}")
        print(f"Raw unknown: {c_raw_unknown}")

    return 0


if __name__ == '__main__':
    raise SystemExit(main())
