#!/usr/bin/env python3
"""
Scan for specific 16-bit IDs that appear with 0x0B 0x04 prefixes in scr*.out.

Pattern searched: 0x0B 0x04 <ID16 little-endian>

Usage:
  python analyzed/scan_prefixed_ids.py scr2.out --ids 4927,4947,4519,1194,1204 --json scr2_prefixed_ids.json

If --json is omitted, prints a compact summary.
Optionally parses the 0x2C-byte header to mark pre/post pointer table with --mark-header.
"""
from __future__ import annotations
import argparse, json, pathlib

HEADER_SIZE = 0x2C

DEFAULT_IDS = [4927, 4947, 4519, 1194, 1204]

def read_header(fbytes: bytes):
    if len(fbytes) < HEADER_SIZE:
        return None
    words = [int.from_bytes(fbytes[i:i+4], 'little') for i in range(0, HEADER_SIZE, 4)]
    return {
        'dialogue_start': words[0],
        'dialogue_end': words[1],
        'pointer_table_start': words[5],
        'pointer_table_end': words[6],
        'raw': words,
    }

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bin_path')
    ap.add_argument('--ids', help='Comma-separated decimal IDs (default known set)')
    ap.add_argument('--json', help='Output JSON path')
    ap.add_argument('--mark-header', action='store_true', help='Annotate pre/post pointer table')
    ap.add_argument('--ctx', type=int, default=16, help='Context bytes before/after (default 16)')
    args = ap.parse_args()

    ids = DEFAULT_IDS
    if args.ids:
        ids = [int(x) for x in args.ids.split(',') if x]

    path = pathlib.Path(args.bin_path)
    data = path.read_bytes()
    header = read_header(data) if args.mark_header else None

    results = {d: [] for d in ids}

    for dec in ids:
        sig = b"\x0b\x04" + dec.to_bytes(2, 'little')
        start = 0
        while True:
            idx = data.find(sig, start)
            if idx == -1:
                break
            # idx points at 0x0B, we want to report the position of 0x0B and of the ID
            id_pos = idx + 2
            before = data[max(0, idx-args.ctx):idx]
            after = data[id_pos+2: id_pos+2+args.ctx]
            entry = {
                'prefix_offset': idx,
                'id_offset': id_pos,
                'offset_hex': f"0x{idx:05x}",
                'bytes_be': f"0x{dec:04x}",
                'bytes_le': dec.to_bytes(2,'little').hex(),
                'context_before': before.hex(),
                'context_after': after.hex(),
            }
            if header:
                entry['pre_ptr_table'] = (idx < header['pointer_table_end'])
            results[dec].append(entry)
            start = idx + 1

    # Output
    if args.json:
        out = {
            'file': str(path),
            'ids': ids,
            'header': header,
            'matches': results,
        }
        pathlib.Path(args.json).write_text(json.dumps(out, indent=2))
    else:
        # Print compact summary
        print('File:', path)
        for dec in ids:
            print(f"ID 0x{dec:04x} ({dec}): {len(results[dec])} hits")

if __name__ == '__main__':
    main()
