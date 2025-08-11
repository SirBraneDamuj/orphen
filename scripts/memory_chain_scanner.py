#!/usr/bin/env python3
"""Locate candidate length-chained script regions in PS2 eeMemory dump.

Hypothesis: The VM uses a second instruction stream where FUN_0025c220 reads a 32-bit value *L* at DAT_00355cd0 and advances the pointer by L. That produces a self-relative length chain.

We attempt to find such chains:
 1. (Optional) Locate a decompressed sector blob (e.g. scr2.out) inside the memory dump to anchor nearby searches.
 2. Scan a window for aligned 32-bit length fields such that repeatedly adding each length stays in-range for several steps (min_steps).
 3. Filter lengths: min_len <= L <= max_len, alignment 4, and break if non-conforming.
 4. Rank chains by number of steps and total span.

Outputs top candidates with stats and a short hex preview for the first few nodes.

Usage:
  python memory_chain_scanner.py --memory eeMemory.bin --sector scr2.out --search-window 0x400000 --min-steps 4

Limitations:
  - False positives possible (random data). Longer consistent chains reduce risk.
  - Does not decode inner opcodes; only structural chain detection.
"""
import argparse, pathlib, os, math
from typing import List, Tuple

def find_sector_offset(memory: bytes, sector: bytes, min_match: int=64) -> int:
    # Try progressively shorter prefix if long prefix fails.
    prefix_len = min(len(sector), 4096)
    while prefix_len >= min_match:
        off = memory.find(sector[:prefix_len])
        if off != -1:
            return off
        prefix_len //= 2
    return -1

def scan_chains(data: bytes, start: int, end: int, min_steps: int=4, min_len: int=4, max_len: int=0x2000, limit: int=50):
    candidates = []
    size = len(data)
    region_end = min(end, size)
    for base in range(start, region_end, 4):
        if base+4 > size: break
        steps = []
        pos = base
        span_end = base
        for depth in range(256):  # safety cap
            if pos+4 > size: break
            L = int.from_bytes(data[pos:pos+4], 'little')
            if not (min_len <= L <= max_len) or (L % 4 != 0):
                break
            steps.append(L)
            pos = pos + L
            if pos > region_end:
                break
            span_end = pos
            if len(steps) >= min_steps:
                # record intermediate (we'll keep best at end)
                pass
        if len(steps) >= min_steps:
            total = sum(steps)
            candidates.append({
                'base': base,
                'steps': len(steps),
                'total_span': total,
                'end': span_end,
                'first_lengths': steps[:8],
                'mean': round(total/len(steps),2)
            })
        if len(candidates) >= limit and limit>0:
            break
    # Rank: more steps first, then larger span
    candidates.sort(key=lambda c:(-c['steps'], -c['total_span']))
    return candidates

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--memory', required=True)
    ap.add_argument('--sector')
    ap.add_argument('--search-window', type=lambda x:int(x,0), default=0x200000, help='Bytes around sector to scan (both directions)')
    ap.add_argument('--min-steps', type=int, default=4)
    ap.add_argument('--min-len', type=int, default=4)
    ap.add_argument('--max-len', type=lambda x:int(x,0), default=0x2000)
    ap.add_argument('--limit', type=int, default=40)
    ap.add_argument('--json', action='store_true')
    args = ap.parse_args()

    mem_path = pathlib.Path(args.memory)
    mem = mem_path.read_bytes()

    anchor = None
    sector_len = 0
    if args.sector:
        sector_bytes = pathlib.Path(args.sector).read_bytes()
        sector_len = len(sector_bytes)
        anchor = find_sector_offset(mem, sector_bytes)
    if anchor is not None and anchor >= 0:
        start = max(0, anchor - args.search_window)
        end = min(len(mem), anchor + sector_len + args.search_window)
    else:
        start = 0
        end = len(mem)
    cands = scan_chains(mem, start, end, args.min_steps, args.min_len, args.max_len, args.limit)
    if args.json:
        import json
        json.dump({'anchor':anchor,'candidates':cands}, open(1,'w'), indent=2)
    else:
        if anchor is not None:
            print(f"Sector anchor: {hex(anchor) if anchor!=-1 else 'not found'} (len={sector_len}) search_range=[0x{start:X},0x{end:X})")
        print(f"Found {len(cands)} candidate chains (showing up to {args.limit})")
        for c in cands[:args.limit]:
            print(f"base=0x{c['base']:08X} steps={c['steps']:3d} span=0x{c['total_span']:X} mean={c['mean']} first={c['first_lengths']}")

if __name__=='__main__':
    main()
