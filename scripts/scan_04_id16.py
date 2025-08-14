#!/usr/bin/env python3
"""
Scan a binary for occurrences of 0x04 followed by two bytes and report the most common ID16s (LE).
Optionally cross-reference with a blocks JSON to report the ID16 immediately after each block's 0x04 closer
(using end or end-1 heuristic).
"""
from __future__ import annotations
import argparse, json
from collections import Counter

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bin_path')
    ap.add_argument('--blocks')
    ap.add_argument('--top', type=int, default=20)
    args = ap.parse_args()

    data = open(args.bin_path, 'rb').read()
    n = len(data)

    # Global scan
    glob = []
    for i in range(0, n-3):
        if data[i] == 0x04:
            glob.append((i, data[i+1:i+3].hex()))
    cnt = Counter([x[1] for x in glob])

    print('Global 0x04<ID16> occurrences:', len(glob))
    print('Top IDs (hex little-endian):', cnt.most_common(args.top))

    if args.blocks:
        blocks = json.load(open(args.blocks, 'r'))
        per_block = []
        miss = 0
        for b in blocks:
            end = b['end']
            term_idx = None
            if 0 <= end < n and data[end] == 0x04:
                term_idx = end
            elif 1 <= end <= n and data[end-1] == 0x04:
                term_idx = end - 1
            if term_idx is None or term_idx+2 >= n:
                miss += 1
                continue
            per_block.append(data[term_idx+1:term_idx+3].hex())
        cc = Counter(per_block)
        print('Per-block end tags found:', len(per_block), 'miss:', miss)
        print('Top per-block IDs:', cc.most_common(args.top))

if __name__ == '__main__':
    main()
