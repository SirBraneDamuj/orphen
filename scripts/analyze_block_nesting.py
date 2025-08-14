#!/usr/bin/env python3
"""Analyze nesting / overlap relationships among extracted block candidates.

Input: JSON array produced by extract_blocks.py (e.g., blocks_scr2.json).
Output: Console summary + optional JSON with per-block parent, depth, relationship flags.

Definitions:
  containment: A contains B if A.start <= B.start and A.end >= B.end and (A.start, A.end)!=(B.start,B.end).
  parent: For each block B, the containing block A with minimal span length (strictly smallest that still contains B).
  depth: root blocks (no parent) depth=0; child depth=parent.depth+1.
  overlap (ambiguous): Two blocks overlap partially without containment (A.start < B.start < A.end < B.end or vice versa).

We treat 'first' vs 'before-next' variant blocks with same start as a special pair. For each start offset, we record variant lengths.

Heuristics to flag suspicious structures:
  * partial_overlap: True if a block overlaps another without containment (likely due to alternative end heuristic).
  * sibling_same_header_collision: siblings sharing identical header bytes (possible repetitive pattern group).

Usage:
  python analyzed/analyze_block_nesting.py blocks_scr2.json --json blocks_scr2_nesting.json --limit 25
"""
from __future__ import annotations
import argparse, json, os, sys
from dataclasses import dataclass, asdict
from typing import List, Optional, Dict

@dataclass
class Block:
    start: int
    end: int
    length: int
    header: str
    first04_distance: int
    end_variant: str
    end_id16: str | None
    counts: Dict[str,int]
    # Derived
    parent: Optional[int] = None  # index of parent in list
    depth: int = 0
    partial_overlap: bool = False
    sibling_same_header_collision: bool = False

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
    # Sort by (start asc, length desc) so larger containers come earlier
    order = sorted(range(len(blocks)), key=lambda i:(blocks[i].start, -blocks[i].length))
    for i in order:
        bi = blocks[i]
        # find minimal container among earlier (since earlier includes larger or equal starts)
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
    # depth pass
    def depth(i: int) -> int:
        b = blocks[i]
        if b.parent is None: return 0
        if blocks[b.parent].depth == -1: # cycle guard
            return 0
        if blocks[b.parent].depth == 0 and blocks[b.parent].parent is not None:
            blocks[b.parent].depth = depth(b.parent)
        return blocks[b.parent].depth + 1
    for b in blocks:
        b.depth = 0
    for i,_ in enumerate(blocks):
        blocks[i].depth = depth(i)

def mark_overlaps(blocks: List[Block]):
    # naive O(n^2) acceptable for current scale
    n = len(blocks)
    for i in range(n):
        bi = blocks[i]
        for j in range(i+1,n):
            bj = blocks[j]
            if bi.start < bj.end and bj.start < bi.end:  # overlap
                if not (bi.start <= bj.start and bi.end >= bj.end) and not (bj.start <= bi.start and bj.end >= bi.end):
                    bi.partial_overlap = True
                    bj.partial_overlap = True

def mark_sibling_header_collisions(blocks: List[Block]):
    from collections import defaultdict
    children_map: Dict[Optional[int], Dict[str,int]] = {}
    for idx,b in enumerate(blocks):
        pid = b.parent
        children_map.setdefault(pid, {})[b.header] = children_map.setdefault(pid, {}).get(b.header,0)+1
    for b in blocks:
        header_counts = children_map.get(b.parent, {})
        if header_counts.get(b.header,0) > 1:
            b.sibling_same_header_collision = True

def variant_pairs(blocks: List[Block]):
    mapping: Dict[int, Dict[str,int]] = {}
    for i,b in enumerate(blocks):
        mapping.setdefault(b.start, {})[b.end_variant] = i
    return mapping

def summarize(blocks: List[Block]):
    import statistics as st
    depths = [b.depth for b in blocks]
    max_depth = max(depths) if depths else 0
    partial = sum(1 for b in blocks if b.partial_overlap)
    variants = variant_pairs(blocks)
    diff_stats = []
    for start, varmap in variants.items():
        if 'first' in varmap and 'before-next' in varmap:
            a = blocks[varmap['first']]
            b = blocks[varmap['before-next']]
            diff_stats.append(b.length - a.length)
    print(f"Blocks: {len(blocks)}")
    print(f"Max depth: {max_depth}")
    print(f"Blocks with partial (non-containment) overlap: {partial}")
    if diff_stats:
        print(f"first->before-next length delta median: {int(st.median(diff_stats))} (min={min(diff_stats)} max={max(diff_stats)})")
    from collections import Counter
    header_by_depth: Dict[int, Counter] = {}
    for b in blocks:
        header_by_depth.setdefault(b.depth, Counter())[b.header] += 1
    for d in sorted(header_by_depth):
        common = header_by_depth[d].most_common(5)
        print(f"Depth {d}: {common}")
    coll = sum(1 for b in blocks if b.sibling_same_header_collision)
    print(f"Blocks with sibling header collisions: {coll}")

def correlate_headers(blocks: List[Block]):
        from collections import defaultdict, Counter
        from typing import TypedDict, List as TList, Dict as TDict
        class HeaderAgg(TypedDict):
            count: int
            root_count: int
            depths: TList[int]
            child_counts: TList[int]
            end_ids: Counter
        children: Dict[int, List[int]] = defaultdict(list)
        for i,b in enumerate(blocks):
            if b.parent is not None:
                children[b.parent].append(i)
        header_stats: Dict[str, HeaderAgg] = {}
        for i,b in enumerate(blocks):
            if b.header not in header_stats:
                header_stats[b.header] = HeaderAgg(count=0, root_count=0, depths=[], child_counts=[], end_ids=Counter())
            hs = header_stats[b.header]
            hs['count'] = hs['count'] + 1
            if b.parent is None:
                hs['root_count'] = hs['root_count'] + 1
            hs['depths'].append(b.depth)
            hs['child_counts'].append(len(children.get(i, [])))
            if b.end_id16:
                hs['end_ids'][b.end_id16] += 1
        print("\nHeader ↔ child/depth correlation:")
        def sort_key(item):
            return item[1]['count']
        for header, stats in sorted(header_stats.items(), key=sort_key, reverse=True):
            import statistics as st
            depths = stats['depths']
            child_counts = stats['child_counts']
            end_ids = stats['end_ids']
            mean_depth = f"{st.mean(depths):.2f}" if depths else '0'
            mean_children = f"{st.mean(child_counts):.2f}" if child_counts else '0'
            uniq_children = sorted(set(child_counts))
            common_end_ids = ', '.join(f"{eid}:{cnt}" for eid,cnt in end_ids.most_common(3)) if end_ids else '-'
            print(f"  {header} | n={stats['count']} roots={stats['root_count']} meanDepth={mean_depth} meanKids={mean_children} childSet={uniq_children} endIDs[{common_end_ids}]")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('blocks_json')
    ap.add_argument('--json-out', help='Write augmented JSON with nesting metadata')
    ap.add_argument('--limit', type=int, default=0, help='Print first N block nesting lines')
    ap.add_argument('--variant', choices=['all','first','before-next'], default='all', help='Restrict analysis to a single heuristic variant')
    ap.add_argument('--header-correlation', action='store_true', help='Compute header ↔ child count/depth correlation metrics')
    args = ap.parse_args()
    blocks = load_blocks(args.blocks_json)
    if args.variant != 'all':
        blocks = [b for b in blocks if b.end_variant == args.variant]
    assign_parents(blocks)
    mark_overlaps(blocks)
    mark_sibling_header_collisions(blocks)
    print(f"== Variant set: {args.variant} ==")
    summarize(blocks)
    if args.header_correlation:
        correlate_headers(blocks)
    if args.limit:
        for i,b in enumerate(sorted(blocks, key=lambda x:(x.start,x.length))[:args.limit]):
            parent_start = '-' if b.parent is None else f"{blocks[b.parent].start:06x}"
            print(f"[{i:03d}] {b.start:06x}-{b.end:06x} len={b.length} depth={b.depth} parent={parent_start} var={b.end_variant} header={b.header} overlap={b.partial_overlap}")
    if args.json_out:
        out = []
        for b in blocks:
            d = asdict(b)
            out.append(d)
        with open(args.json_out,'w') as f:
            json.dump(out,f,indent=2)
            print(f"Wrote augmented JSON -> {args.json_out}")

if __name__ == '__main__':
    main()
