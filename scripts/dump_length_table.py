#!/usr/bin/env python3
"""
Dump dialogue/cutscene opcode length table from an EE memory snapshot.

Target: Address 0x0031c518 (PTR_DAT_0031c518) confirmed via RE as per-opcode length table
used by FUN_00237ca0 / FUN_00238c08 for opcodes < 0x1F (and possibly beyond).

We read a configurable number of bytes (default 0x80) starting at that address
and emit JSON and a readable table for quick inspection.

Usage:
  python dump_length_table.py --mem eeMemory.bin --addr 0x0031c518 --count 0x80 --json
"""
import argparse, json, pathlib, sys

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mem', required=True, help='Path to eeMemory.bin snapshot')
    ap.add_argument('--addr', type=lambda x:int(x,0), default=0x0031c518, help='Start address of table')
    ap.add_argument('--count', type=lambda x:int(x,0), default=0x80, help='Bytes to dump')
    ap.add_argument('--json', action='store_true')
    args = ap.parse_args()

    data = pathlib.Path(args.mem).read_bytes()
    if args.addr + args.count > len(data):
        print(f'Requested span exceeds file (file_size=0x{len(data):X})', file=sys.stderr)
        span = data[args.addr:]
    else:
        span = data[args.addr: args.addr+args.count]

    entries = []
    for idx, b in enumerate(span):
        entries.append({'index': idx, 'hex_index': f'0x{idx:02X}', 'len': b})

    if args.json:
        json.dump({'base_address': f'0x{args.addr:08X}', 'count': len(span), 'entries': entries}, sys.stdout, indent=2)
    else:
        print(f'Length table @ 0x{args.addr:08X} (dumped {len(span)} bytes)')
        for e in entries:
            print(f"{e['hex_index']}: {e['len']:2d}")
        # Quick highlight for first 0x20 opcodes
        print('\nFirst 0x20 opcode lengths:')
        for e in entries[:0x20]:
            print(f"{e['hex_index']}: {e['len']:2d}", end='  ')
        print()

if __name__ == '__main__':
    main()
