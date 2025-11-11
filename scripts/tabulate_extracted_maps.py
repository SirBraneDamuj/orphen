#!/usr/bin/env python3
"""
Tabulate magic types from already-extracted map segments in out/maps.

Decision per index (####):
- If map_####.psm2 exists → classify as PSM2
- Else if map_####.dec.bin exists → classify by its first 4 bytes (uppercased ASCII)
- Else → classify by map_####.bin first 4 bytes

Print counts for PSM2, PSC3, PSB4, OTHER and total.
"""

from __future__ import annotations

import argparse
import os
import re
from collections import Counter

KNOWN = {b"PSM2", b"PSC3", b"PSB4"}

def magic4(path: str) -> bytes:
    try:
        with open(path, 'rb') as f:
            return f.read(4).upper()
    except Exception:
        return b""


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Tabulate magic types in out/maps")
    ap.add_argument("out_dir", nargs="?", default="out/maps", help="Directory with extracted files")
    args = ap.parse_args(argv)

    rx = re.compile(r"^map_(\d{4})\.(psm2|dec\.bin|bin)$", re.IGNORECASE)

    # group by index
    by_idx: dict[str, dict[str, str]] = {}
    for name in os.listdir(args.out_dir):
        m = rx.match(name)
        if not m:
            continue
        idx, ext = m.group(1), m.group(2).lower()
        d = by_idx.setdefault(idx, {})
        d[ext] = os.path.join(args.out_dir, name)

    counts = Counter()
    other_examples = []

    for idx, files in sorted(by_idx.items()):
        tag = "OTHER"
        if 'psm2' in files:
            tag = 'PSM2'
        elif 'dec.bin' in files:
            m = magic4(files['dec.bin']).decode('ascii', 'ignore')
            if m in { 'PSC3', 'PSB4', 'PSM2' }:
                tag = m
            else:
                tag = 'OTHER'
                other_examples.append((idx, m))
        elif 'bin' in files:
            m = magic4(files['bin']).decode('ascii', 'ignore')
            if m in { 'PSC3', 'PSB4', 'PSM2' }:
                tag = m
            else:
                tag = 'OTHER'
                other_examples.append((idx, m))
        counts[tag] += 1

    total = sum(counts.values())
    print(f"Total indices found: {total}")
    for key in ["PSM2", "PSC3", "PSB4", "OTHER"]:
        print(f"{key:5}: {counts.get(key, 0)}")
    if other_examples:
        print("\nExamples of OTHER (index -> magic):")
        for idx, m in other_examples[:10]:
            print(f"  {idx} -> {m}")

    return 0

if __name__ == '__main__':
    import sys
    raise SystemExit(main(sys.argv[1:]))
