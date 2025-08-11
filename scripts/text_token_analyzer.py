#!/usr/bin/env python3
"""Quick analyzer for decompressed Orphen script blobs.

Outputs:
- File length
- Frequency of control bytes (<0x20 except common whitespace 0x09,0x0A,0x0D)
- Frequency of high-bit bytes (>=0x80)
- Top N by frequency for each group
- Context windows around each distinct high-bit/control byte (limited samples)

Usage:
  python scripts/text_token_analyzer.py SCR_UNSECTORED.out --limit 8 --samples 5
"""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
from pathlib import Path

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('file')
    ap.add_argument('--limit', type=int, default=16, help='Top N tokens to show per class')
    ap.add_argument('--samples', type=int, default=5, help='Max context samples per token')
    ap.add_argument('--width', type=int, default=12, help='Context characters each side')
    args = ap.parse_args()

    data = Path(args.file).read_bytes()
    n = len(data)
    ctrl_counter = Counter()
    high_counter = Counter()
    ctrl_bytes = set()
    high_bytes = set()
    contexts = defaultdict(list)

    skip_ctrl = {0x09,0x0A,0x0D,0x00}

    for i,b in enumerate(data):
        if b < 0x20 and b not in skip_ctrl:
            ctrl_counter[b]+=1
            if len(contexts[b]) < args.samples:
                start=max(0,i-args.width)
                end=min(n,i+args.width)
                snippet = data[start:end]
                contexts[b].append((i,snippet))
        if b >= 0x80:
            high_counter[b]+=1
            if len(contexts[b]) < args.samples:
                start=max(0,i-args.width)
                end=min(n,i+args.width)
                snippet = data[start:end]
                contexts[b].append((i,snippet))

    def fmt_byte(b):
        return f'0x{b:02X}'

    print(f'File length: {n}')
    print('\nControl (<0x20) counts (excluding whitespace), top {args.limit}:')
    for b,c in ctrl_counter.most_common(args.limit):
        print(f'  {fmt_byte(b)}: {c}')
    print('\nHigh-bit (>=0x80) counts, top {args.limit}:')
    for b,c in high_counter.most_common(args.limit):
        print(f'  {fmt_byte(b)}: {c}')

    print('\nSample contexts: (offset: surrounding bytes, printable ASCII shown)')
    def printable(bs:bytes):
        return ''.join(chr(x) if 0x20<=x<0x7F else '.' for x in bs)
    for b in sorted(contexts, key=lambda x: (x<0x20, x)):
        print(f'\nToken {fmt_byte(b)}:')
        for off,snip in contexts[b]:
            print(f'  @{off:06X}: {printable(snip)}')

if __name__ == '__main__':
    main()
