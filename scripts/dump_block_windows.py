#!/usr/bin/env python3
"""Dump raw byte windows for selected block headers.

Targets the 'ladder' headers sequence previously identified:
  5d000000 -> 48000000 -> 33000000 -> 1e000000 -> 09000000

Input:
  * blocks JSON (first-variant only) e.g. blocks_scr2_first_v2.json
  * binary script file (e.g. scr2.out)

Process:
  * Recompute parent/child nesting (same method as analyze_block_nesting) for context.
  * Select blocks whose header is in the target set.
  * For each, output:
      - header, start, end, length, computed depth
      - parent chain headers (top->this)
      - first_window: bytes from start to start+N (default 48) (clamped)
      - last_window: bytes from end-M to end (default 32) including terminator 0x04
      - window hex grouped for readability

Usage:
  python analyzed/dump_block_windows.py blocks_scr2_first_v2.json scr2.out --out ladder_windows.txt
"""
from __future__ import annotations
import argparse, json, os, sys
from dataclasses import dataclass
from typing import List, Optional, Dict

TARGET_HEADERS = ["5d000000","48000000","33000000","1e000000","09000000"]

@dataclass
class Block:
    start: int
    end: int
    length: int
    header: str
    first04_distance: int
    end_variant: str
    end_id16: Optional[str]
    counts: Dict[str,int]
    parent: Optional[int] = None
    depth: int = 0

def load_blocks(path: str) -> List[Block]:
    data = json.load(open(path,'r'))
    blocks: List[Block] = []
    for obj in data:
        blocks.append(Block(
            start=obj['start'], end=obj['end'], length=obj['length'], header=obj.get('header',''),
            first04_distance=obj.get('first04_distance',-1), end_variant=obj.get('end_variant',''),
            end_id16=obj.get('end_id16'), counts=obj.get('counts',{})
        ))
    return blocks

def assign_parents(blocks: List[Block]):
    order = sorted(range(len(blocks)), key=lambda i:(blocks[i].start, -blocks[i].length))
    for i in order:
        bi = blocks[i]
        parent_idx = None
        parent_span = None
        for j in order:
            if j == i: continue
            bj = blocks[j]
            if bj.start <= bi.start and bj.end >= bi.end and (bj.start != bi.start or bj.end != bi.end):
                span = bj.end - bj.start
                if parent_span is None or span < parent_span:
                    parent_span = span
                    parent_idx = j
        bi.parent = parent_idx
    def depth(i: int) -> int:
        b = blocks[i]
        if b.parent is None:
            return 0
        return depth(b.parent) + 1
    for idx in range(len(blocks)):
        blocks[idx].depth = depth(idx)

def hex_bytes(b: bytes) -> str:
    out_lines = []
    for i in range(0,len(b),16):
        chunk = b[i:i+16]
        out_lines.append(' '.join(f"{x:02x}" for x in chunk))
    return '\n'.join(out_lines)

def parent_chain(blocks: List[Block], idx: int) -> List[Block]:
    chain = []
    cur = idx
    while cur is not None:
        chain.append(blocks[cur])
        cur = blocks[cur].parent
    return list(reversed(chain))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('blocks_json')
    ap.add_argument('binary')
    ap.add_argument('--headers', nargs='*', default=TARGET_HEADERS, help='Headers to dump (hex lowercase)')
    ap.add_argument('--first-window', type=int, default=48)
    ap.add_argument('--last-window', type=int, default=32)
    ap.add_argument('--out', help='Write output to file (else stdout)')
    args = ap.parse_args()
    blocks = load_blocks(args.blocks_json)
    assign_parents(blocks)
    data = open(args.binary,'rb').read()
    lines = []
    header_set = set(args.headers)
    targets = [ (i,b) for i,b in enumerate(blocks) if b.header in header_set ]
    if not targets:
        print("No blocks matched headers", file=sys.stderr)
        sys.exit(1)
    for idx,b in targets:
        fw = data[b.start: min(len(data), b.start + args.first_window)]
        lw = data[max(0, b.end - args.last_window): b.end]
        chain = parent_chain(blocks, idx)
        chain_str = ' -> '.join(f"{c.header}@{c.start:06x}" for c in chain)
        lines.append(f"HEADER {b.header} start=0x{b.start:06x} end=0x{b.end:06x} len={b.length} depth={b.depth} end_id={b.end_id16}\n")
        lines.append(f"CHAIN {chain_str}\n")
        lines.append("FIRST_WINDOW:\n" + hex_bytes(fw) + "\n")
        lines.append("LAST_WINDOW:\n" + hex_bytes(lw) + "\n")
        lines.append('-'*72 + '\n')
    out_text = ''.join(lines)
    if args.out:
        with open(args.out,'w') as f:
            f.write(out_text)
        print(f"Wrote {len(targets)} block windows -> {args.out}")
    else:
        print(out_text)

if __name__ == '__main__':
    main()
