#!/usr/bin/env python3
"""Heuristic block extractor for Orphen script bytecode (first-0x04 variant ONLY).

Current accepted model:
    * 0x32 opens a structural block.
    * The FIRST subsequent 0x04 marks its end (pop). Earlier "before-next" heuristic
        (extending to the last 0x04 before the next 0x32) produced flattened, over-merged
        regions and has been removed.

Output per block:
    - start (offset of 0x32)
    - end (exclusive, one past chosen 0x04)
    - length
    - header (4 bytes after 0x32)
    - first04_distance
    - end_variant (always 'first' now; retained for backward compatibility)
    - end_id16 (little-endian 16-bit after 0x04, if bytes available)
    - counts of key marker bytes inside span

Usage:
    python analyzed/extract_blocks.py scr2.out --json blocks_first.json

NOTE: Historical args --mode before-next / both are no longer supported and will error.
"""
from __future__ import annotations
import argparse, json, os, sys
from dataclasses import dataclass, asdict
from typing import List, Optional

KEY_BYTES = [0x04, 0x32, 0x0b, 0x0e, 0x1e, 0x79]

@dataclass
class BlockCandidate:
    start: int
    end: int  # exclusive
    length: int
    header: str
    first04_distance: int
    end_variant: str
    end_id16: Optional[str]
    counts: dict

def find_positions(data: bytes, value: int) -> List[int]:
    return [i for i,b in enumerate(data) if b == value]

def little_id_after(data: bytes, pos: int) -> Optional[str]:
    if pos + 2 < len(data):
        lo = data[pos+1]
        hi = data[pos+2]
        return f"0x{(lo | (hi<<8)):04x}"
    return None

def count_key_bytes(data: bytes, start: int, end: int) -> dict:
    window = data[start:end]
    return {f"0x{k:02x}": window.count(k) for k in KEY_BYTES}

def extract_blocks(data: bytes) -> List[BlockCandidate]:
    pos32 = find_positions(data, 0x32)
    pos04 = find_positions(data, 0x04)
    candidates: List[BlockCandidate] = []
    for start in pos32:
        header = data[start+1:start+5].hex() if start+5 <= len(data) else ''
        first04 = next((p for p in pos04 if p > start), None)
        first04_distance = (first04 - start) if first04 is not None else -1
        if first04 is None:
            continue  # cannot form a block without terminator
        end_excl = min(len(data), first04+1)
        counts = count_key_bytes(data, start, end_excl)
        end_id = little_id_after(data, first04)
        candidates.append(BlockCandidate(
            start=start,
            end=end_excl,
            length=end_excl-start,
            header=header,
            first04_distance=first04_distance,
            end_variant='first',
            end_id16=end_id,
            counts=counts,
        ))
    return candidates

def summarize(candidates: List[BlockCandidate], top: int = 10):
    from collections import Counter
    header_ct = Counter(c.header for c in candidates)
    end_id_ct = Counter(c.end_id16 for c in candidates if c.end_id16)
    print(f"Total block candidates: {len(candidates)}")
    print("Top headers:")
    for h,cnt in header_ct.most_common(top):
        print(f"  {h or '(short)'} : {cnt}")
    print("Top end IDs (raw little-endian after 0x04):")
    for i,cnt in end_id_ct.most_common(top):
        print(f"  {i} : {cnt}")
    lengths = [c.length for c in candidates]
    if lengths:
        import statistics as st
        print(f"Length min/median/max: {min(lengths)}/{int(st.median(lengths))}/{max(lengths)}")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('file')
    ap.add_argument('--json', help='Write JSON list of candidates')
    ap.add_argument('--limit', type=int, default=0, help='Limit number of printed block summaries')
    # Keep deprecated --mode for backward compatibility; reject anything not 'first'.
    ap.add_argument('--mode', default='first', help='[deprecated] must be "first" now; other values invalid')
    args = ap.parse_args()
    if args.mode != 'first':
        print(f"ERROR: before-next / both heuristics removed; only 'first' is supported.", file=sys.stderr)
        sys.exit(2)
    if not os.path.isfile(args.file):
        print(f"File not found: {args.file}", file=sys.stderr)
        sys.exit(1)
    data = open(args.file,'rb').read()
    cands = extract_blocks(data)
    summarize(cands)
    if args.limit:
        for c in cands[:args.limit]:
            print(f"Block {c.start:06x}-{c.end:06x} len={c.length} header={c.header} end_variant={c.end_variant} end_id={c.end_id16}")
    if args.json:
        with open(args.json,'w') as f:
            json.dump([asdict(c) for c in cands], f, indent=2)
            print(f"Wrote {len(cands)} candidates -> {args.json}")

if __name__ == '__main__':
    main()
