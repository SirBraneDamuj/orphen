#!/usr/bin/env python3
"""
Tabulate segment magic types in MAP.BIN, preferring decoded headers.

Classification logic per segment:
- Read raw segment (sector_offset * 2048, length = size_words * 4)
- magic_raw = first 4 bytes (uppercased ASCII)
- Attempt headerless LZ decode; magic_dec = first 4 bytes of decoded (uppercased ASCII)
- If magic_dec in {PSM2, PSC3, PSB4}: use magic_dec
  elif magic_raw in {PSM2, PSC3, PSB4}: use magic_raw
  else: use 'OTHER'

Print overall counts and a small breakdown of raw vs decoded observations.
"""

from __future__ import annotations

import argparse
import os
import struct
from collections import Counter
from typing import Tuple

try:
    from orphen_lz_headerless_decoder import decode_bytes
except Exception:
    decode_bytes = None  # type: ignore

SECTOR_SIZE = 2048
KNOWN = {b"PSM2", b"PSC3", b"PSB4"}


def parse_entry(raw: int) -> Tuple[int, int]:
    size_words = raw & 0x1FFFF  # 17 bits
    sector_off = (raw >> 17) & 0x7FFF  # 15 bits
    return sector_off, size_words * 4


def magic4(b: bytes) -> bytes:
    return (b[:4] if len(b) >= 4 else b).upper()


def classify_segment(seg: bytes) -> Tuple[str, str, str]:
    """Return (best, raw, dec) magic tags as strings.
    best chooses decoded if it matches a known magic, else raw if known, else OTHER.
    """
    raw_m = magic4(seg)
    dec_m = b""
    if decode_bytes is not None:
        try:
            dec = decode_bytes(seg)
            dec_m = magic4(dec)
        except Exception:
            dec_m = b""
    best: bytes
    if dec_m in KNOWN:
        best = dec_m
    elif raw_m in KNOWN:
        best = raw_m
    else:
        best = b"OTHER"
    # Return as strings
    return best.decode('ascii', 'ignore'), raw_m.decode('ascii', 'ignore'), dec_m.decode('ascii', 'ignore')


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Tabulate MAP.BIN magic types (prefer decoded)")
    ap.add_argument("map_bin", help="Path to MAP.BIN")
    ap.add_argument("--limit", type=int, default=None)
    args = ap.parse_args(argv)

    with open(args.map_bin, 'rb') as f:
        hdr = f.read(4)
        if len(hdr) < 4:
            raise SystemExit("MAP.BIN too small")
        (entry_count,) = struct.unpack('<I', hdr)
        entries = [struct.unpack('<I', f.read(4))[0] for _ in range(entry_count)]

        counts = Counter()
        counts_raw = Counter()
        counts_dec = Counter()

        for idx, raw in enumerate(entries):
            if args.limit is not None and idx >= args.limit:
                break
            sec, size = parse_entry(raw)
            if size == 0:
                continue
            f.seek(sec * SECTOR_SIZE)
            seg = f.read(size)
            best, raw_m, dec_m = classify_segment(seg)
            counts[best] += 1
            counts_raw[raw_m or ''] += 1
            counts_dec[dec_m or ''] += 1

    total = sum(counts.values())
    print(f"Total segments considered: {total}")
    for key in ["PSM2", "PSC3", "PSB4", "OTHER"]:
        print(f"{key:5}: {counts.get(key, 0)}")

    # Optional extra breakdown
    print("\nRaw magic top 10:")
    for k, v in counts_raw.most_common(10):
        if not k:
            continue
        print(f"  {k:5}: {v}")

    if decode_bytes is not None:
        print("\nDecoded magic top 10:")
        for k, v in counts_dec.most_common(10):
            if not k:
                continue
            print(f"  {k:5}: {v}")

    return 0


if __name__ == '__main__':
    raise SystemExit(main())
