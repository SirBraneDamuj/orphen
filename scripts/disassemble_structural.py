#!/usr/bin/env python3
"""
Structural disassembler for Orphen script blocks, mirroring FUN_0025bc68 control flow.

- Uses blocks_scr2_first_v2.json (or user-provided blocks JSON) for bounded block ranges.
- Prints a readable listing with nesting, prologue bytes, and key events (BLOCK_OPEN, VM_ADVANCE, BLOCK_END).
- Classifies opcodes into low (<0x0b), high (>=0x32), and extended (0xff next).

Usage examples:
  python analyzed/disassemble_structural.py scr2.out --blocks blocks_scr2_first_v2.json --index 0
  python analyzed/disassemble_structural.py scr2.out --blocks blocks_scr2_first_v2.json --all --out scr2_structural_disasm.txt
"""
from __future__ import annotations
import argparse, json, sys
from typing import List, Optional, Set

LOW_MAX = 0x0b  # low ops are 0x00..0x0a
BLOCK_OPEN = 0x32
EXT_PREFIX = 0xff
BLOCK_END = 0x04

class Printer:
    def __init__(self, out, indent_size=2):
        self.out = out
        self.indent = 0
        self.indent_size = indent_size
    def push(self):
        self.indent += 1
    def pop(self):
        self.indent = max(0, self.indent-1)
    def line(self, s: str):
        self.out.write(' ' * (self.indent * self.indent_size) + s + '\n')

def classify_op(b: int) -> str:
    if b < LOW_MAX:
        if b == BLOCK_END:
            return 'END_04'
        return f'LOW_{b:02x}'
    if b == EXT_PREFIX:
        return 'EXT_FF'
    if b == BLOCK_OPEN:
        return 'OPEN_32'
    return f'HIGH_{b:02x}'

def read_u8(data: bytes, off: int) -> int:
    return data[off]

def read_u16le(data: bytes, off: int) -> int:
    return data[off] | (data[off+1] << 8)

def read_u32le(data: bytes, off: int) -> int:
    return data[off] | (data[off+1] << 8) | (data[off+2] << 16) | (data[off+3] << 24)

def disasm_block(data: bytes, start: int, out, max_steps: int = 100000, *, compress_zero: bool=False, pad_threshold:int=4, highlight_ids: Optional[Set[int]]=None) -> None:
    pr = Printer(out)
    pb = start
    n = len(data)
    cont_stack: List[int] = []
    steps = 0

    pr.line(f"; block_start 0x{start:06x}")

    while True:
        if steps >= max_steps:
            pr.line("; max steps reached")
            break
        if not (0 <= pb < n):
            pr.line(f"; pb out of range 0x{pb:06x}")
            break
        b = data[pb]
        pb1 = pb + 1
        steps += 1

        # Special-case known SUBPROC pattern: 0x0B 0x04 <ID16>
        if b == 0x0b and pb + 3 < n and data[pb+1] == 0x04:
            id_le = read_u16le(data, pb+2)
            tag = f" id16=0x{id_le:04x}"
            if highlight_ids and id_le in highlight_ids:
                tag += " [HIT]"
            pr.line(f"{pb:06x}: 0b 04 {data[pb+2]:02x} {data[pb+3]:02x} ; SUBPROC{tag}")
            pb += 4
            continue

        if b < LOW_MAX:
            if b == BLOCK_END:
                # Record end tag if present
                end_id = data[pb+1:pb+3].hex() if pb+3 <= n else None
                pr.line(f"{pb:06x}: 04    ; BLOCK_END end_id16_le={end_id}")
                if cont_stack:
                    pb = cont_stack.pop()
                    pr.pop()
                    continue
                else:
                    # Finished top-level block
                    break
            else:
                # Optional compression of zero padding
                if compress_zero and b == 0x00:
                    z = pb
                    while z < n and data[z] == 0x00 and (z - pb) < 0x10000:
                        z += 1
                    run = z - pb
                    if run >= pad_threshold:
                        pr.line(f"{pb:06x}: 00 .. ; PAD_00 x{run}")
                        pb = z
                        continue
                # Decode selected immediates per FUN_0025bf70 cases if encountered in structural stream
                if b == 0x0c and pb+1 < n:
                    imm = read_u8(data, pb+1)
                    pr.line(f"{pb:06x}: 0c {imm:02x} ; LOW_0c imm8={imm}")
                    pb += 2
                elif b == 0x0d and pb+2 < n:
                    imm = read_u16le(data, pb+1)
                    pr.line(f"{pb:06x}: 0d {data[pb+1]:02x} {data[pb+2]:02x} ; LOW_0d imm16={imm}")
                    pb += 3
                elif b == 0x0e and pb+4 < n:
                    imm = read_u32le(data, pb+1)
                    pr.line(f"{pb:06x}: 0e {data[pb+1]:02x} {data[pb+2]:02x} {data[pb+3]:02x} {data[pb+4]:02x} ; LOW_0e imm32=0x{imm:08x}")
                    pb += 5
                elif b == 0x0f and pb+4 < n:
                    val = read_u32le(data, pb+1)
                    pr.line(f"{pb:06x}: 0f {data[pb+1]:02x} {data[pb+2]:02x} {data[pb+3]:02x} {data[pb+4]:02x} ; LOW_0f imm32={val} scaled=\"*100\"")
                    pb += 5
                elif b == 0x10 and pb+2 < n:
                    imm = read_u16le(data, pb+1)
                    pr.line(f"{pb:06x}: 10 {data[pb+1]:02x} {data[pb+2]:02x} ; LOW_10 imm16={imm} scaled=\"*1000\"")
                    pb += 3
                elif b == 0x11 and pb+2 < n:
                    imm = read_u16le(data, pb+1)
                    pr.line(f"{pb:06x}: 11 {data[pb+1]:02x} {data[pb+2]:02x} ; LOW_11 imm16={imm} scaled=\"*0xf570/0x168\"")
                    pb += 3
                elif b in (0x30, 0x31):
                    # Composite immediate built from VM expressions; we can only annotate size
                    pr.line(f"{pb:06x}: {b:02x}    ; LOW_{b:02x} tuple_immediates count={'4' if b==0x31 else '3'}")
                    pb = pb1
                else:
                    pr.line(f"{pb:06x}: {b:02x}    ; {classify_op(b)}")
                    pb = pb1
                continue
        elif b == EXT_PREFIX:
            if pb+1 >= n:
                pr.line(f"{pb:06x}: ff ?? ; EXT_FF (truncated)")
                break
            nxt = data[pb+1]
            pr.line(f"{pb:06x}: ff {nxt:02x} ; EXT_FF_{nxt:02x}")
            pb += 2
            continue
        elif b == BLOCK_OPEN:
            # push continuation after 4 prologue bytes
            cont = pb + 5
            cont_stack.append(cont)
            pr.line(f"{pb:06x}: 32    ; BLOCK_OPEN push_cont=0x{cont:06x}")
            pr.push()
            # Advance to first prologue opcode
            pb = pb1
            # Prologue: next four bytes are opcodes; log them and advance
            for i in range(4):
                if pb >= n:
                    pr.line("; prologue truncated")
                    break
                op = data[pb]
                if op == EXT_PREFIX and pb+1 < n:
                    pr.line(f"{pb:06x}: ff {data[pb+1]:02x} ; PROLOGUE {classify_op(op)}")
                    pb += 2
                else:
                    # Also recognize SUBPROC inside prologue if it appears
                    if op == 0x0b and pb+3 < n and data[pb+1] == 0x04:
                        id_le = read_u16le(data, pb+2)
                        tag = f" id16=0x{id_le:04x}"
                        if highlight_ids and id_le in highlight_ids:
                            tag += " [HIT]"
                        pr.line(f"{pb:06x}: 0b 04 {data[pb+2]:02x} {data[pb+3]:02x} ; PROLOGUE SUBPROC{tag}")
                        pb += 4
                    else:
                        pr.line(f"{pb:06x}: {op:02x}    ; PROLOGUE {classify_op(op)}")
                        pb += 1
            # Structural calls FUN_0025c220 here (VM relative jump); annotate only
            pr.line("; VM_ADVANCE (FUN_0025c220)")
            continue
        else:
            pr.line(f"{pb:06x}: {b:02x}    ; {classify_op(b)}")
            pb = pb1
            continue

    pr.line(f"; block_end 0x{start:06x}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bin_path')
    ap.add_argument('--blocks', help='Blocks JSON (first-0x04 canonical) to get block starts')
    ap.add_argument('--index', type=int, help='Index into blocks JSON to disassemble')
    ap.add_argument('--start', type=lambda x: int(x,0), help='Manual start offset (0x...)')
    ap.add_argument('--all', action='store_true', help='Disassemble all blocks from JSON')
    ap.add_argument('--out', help='Output path (defaults to stdout)')
    ap.add_argument('--compress-zero', action='store_true', help='Compress long runs of 0x00 as PAD_00')
    ap.add_argument('--pad-threshold', type=int, default=4, help='Minimum run length to compress (default 4)')
    ap.add_argument('--highlight-ids', help='Comma-separated hex or dec IDs to highlight for SUBPROC (e.g., 4927,0x04aa)')
    args = ap.parse_args()

    data = open(args.bin_path, 'rb').read()
    out = open(args.out, 'w') if args.out else sys.stdout

    if args.all and not args.blocks:
        print('--all requires --blocks', file=sys.stderr)
        sys.exit(2)

    if args.blocks:
        blocks = json.load(open(args.blocks,'r'))
    else:
        blocks = []

    # Parse highlight IDs
    hi_set: Optional[Set[int]] = None
    if args.highlight_ids:
        hi_set = set()
        for tok in args.highlight_ids.split(','):
            tok = tok.strip()
            if not tok:
                continue
            if tok.lower().startswith('0x'):
                hi_set.add(int(tok, 16))
            else:
                hi_set.add(int(tok))

    if args.start is not None:
        disasm_block(data, args.start, out, compress_zero=args.compress_zero, pad_threshold=args.pad_threshold, highlight_ids=hi_set)
    elif args.blocks and args.index is not None:
        b = blocks[args.index]
        disasm_block(data, b['start'], out, compress_zero=args.compress_zero, pad_threshold=args.pad_threshold, highlight_ids=hi_set)
    elif args.all and args.blocks:
        for i,b in enumerate(blocks):
            out.write(f"\n; ===== BLOCK {i} start=0x{b['start']:06x} =====\n")
            disasm_block(data, b['start'], out, compress_zero=args.compress_zero, pad_threshold=args.pad_threshold, highlight_ids=hi_set)
    else:
        print('Provide --start or --blocks with --index/--all', file=sys.stderr)
        sys.exit(1)

    if out is not sys.stdout:
        out.close()

if __name__ == '__main__':
    main()
