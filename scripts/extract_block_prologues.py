#!/usr/bin/env python3
"""
Extract structural block prologues (the 4 opcodes following 0x32) and related metadata.

Two modes:
- With --blocks: Use a canonical blocks JSON (first-0x04 heuristic) to get start/end.
- Without --blocks: Scan the binary for 0x32 and find the first 0x04 after each start.

Output JSON entries:
{
  start: int,          # position of 0x32
  prologue: [int,int,int,int],
  prologue_hex: str,   # 8 hex chars
  first_opcode: int,   # prologue[0]
  body_start: int,     # start + 5 (after 0x32 + 4 prologue bytes)
  end: int,            # index of 0x04 terminator (first variant)
  end_id16: str        # 4 hex chars of the two bytes following 0x04 (little endian order preserved as hex)
}
"""
from __future__ import annotations
import argparse, json, sys
from collections import Counter

def find_first(data: bytes, value: int, start: int) -> int:
    try:
        return data.index(bytes([value]), start)
    except ValueError:
        return -1

def hex_bytes(bs: bytes) -> str:
    return bs.hex()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bin_path', help='Input script binary (e.g., scr2.out)')
    ap.add_argument('--out', required=True, help='Output JSON path')
    ap.add_argument('--blocks', help='Existing blocks JSON to use for start/end')
    ap.add_argument('--print-stats', action='store_true')
    args = ap.parse_args()

    data = open(args.bin_path, 'rb').read()

    entries = []
    starts = []

    if args.blocks:
        blocks = json.load(open(args.blocks, 'r'))
        for b in blocks:
            start = b['start']
            end = b['end']
            if start < 0 or start+5 > len(data):
                continue
            if data[start] != 0x32:
                # Skip non-32 safety
                continue
            pro = data[start+1:start+5]
            pro_list = list(pro)
            entry = {
                'start': start,
                'prologue': pro_list,
                'prologue_hex': hex_bytes(pro),
                'first_opcode': pro_list[0],
                'body_start': start + 5,
                'end': end,
            }
            # Heuristic: dataset may store end as exclusive; capture 0x04 at end or end-1
            term_idx = None
            if 0 <= end < len(data) and data[end] == 0x04:
                term_idx = end
            elif 1 <= end <= len(data) and data[end-1] == 0x04:
                term_idx = end - 1
            if term_idx is not None and term_idx + 2 < len(data):
                entry['end_id16'] = data[term_idx+1:term_idx+3].hex()
            else:
                entry['end_id16'] = None
            entries.append(entry)
            starts.append(start)
    else:
        # Scan mode
        i = 0
        n = len(data)
        while True:
            i = find_first(data, 0x32, i)
            if i < 0:
                break
            start = i
            end = find_first(data, 0x04, start+1)
            if end < 0:
                i = start + 1
                continue
            if start+5 > n:
                i = start + 1
                continue
            pro = data[start+1:start+5]
            pro_list = list(pro)
            entry = {
                'start': start,
                'prologue': pro_list,
                'prologue_hex': hex_bytes(pro),
                'first_opcode': pro_list[0],
                'body_start': start + 5,
                'end': end,
            }
            term_idx = None
            if 0 <= end < n and data[end] == 0x04:
                term_idx = end
            elif 1 <= end <= n and data[end-1] == 0x04:
                term_idx = end - 1
            if term_idx is not None and term_idx + 2 < n:
                entry['end_id16'] = data[term_idx+1:term_idx+3].hex()
            else:
                entry['end_id16'] = None
            entries.append(entry)
            i = start + 1

    # Write output
    with open(args.out, 'w') as f:
        json.dump(entries, f, indent=2)

    if args.print_stats:
        c_first = Counter(e['first_opcode'] for e in entries)
        print('Total blocks:', len(entries))
        print('Top first opcodes:', c_first.most_common(10))
        c_pro = Counter(e['prologue_hex'] for e in entries)
        print('Top prologue hex:', c_pro.most_common(10))
        c_endid = Counter(e['end_id16'] for e in entries if e['end_id16'])
        print('Top end_id16:', c_endid.most_common(10))

if __name__ == '__main__':
    main()
