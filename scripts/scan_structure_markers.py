#!/usr/bin/env python3
"""Scan a script binary for structural opcode relationships.

Focus:
  * 0x04 <ID16> patterns (ID treated little-endian: low byte immediately after 0x04)
  * 0x32 opcode (candidate BLOCK_BEGIN) and the 4 bytes immediately following (header payload hypothesis)
  * Distance statistics between 0x32 and the next 0x04

Outputs:
  - Counts / frequencies for IDs after 0x04
  - Top header 4-byte patterns after 0x32
  - Distance histogram (bucketed)
  - Sample contexts around occurrences

Usage:
  python analyzed/scan_structure_markers.py scr2.out --sample 40 --max-distance 4096
"""
from __future__ import annotations
import argparse
import os, sys, random, math
from collections import Counter, defaultdict


def hexdump_slice(data: bytes, center: int, radius: int = 16) -> str:
    start = max(0, center - radius)
    end = min(len(data), center + radius)
    frag = data[start:end]
    hex_bytes = ' '.join(f"{b:02x}" for b in frag)
    caret_pos = center - start
    # Build a marker line with ^^ under the opcode byte
    marker = ' '.join(('^^' if i == caret_pos else '  ') for i in range(len(frag)))
    return f"[{start:06x}:{end:06x}]\n{hex_bytes}\n{marker}"


def bucket_distance(dist: int) -> str:
    if dist < 0:
        return "neg"
    # logarithmic-ish buckets
    if dist < 16: return f"{dist}"  # small exact
    for edge in (32, 64, 128, 256, 512, 1024, 2048, 4096, 8192):
        if dist < edge:
            return f"<{edge}"
    return ">=8192"


def analyze(path: str, sample: int, max_distance: int):
    with open(path, 'rb') as f:
        data = f.read()
    n = len(data)
    op04_positions = []
    op32_positions = []
    id_counter = Counter()
    header_counter = Counter()
    distances = Counter()
    next04_cache = []

    # Precompute next 0x04 position for each 0x32 (single pass)
    # Build list of 0x04 positions
    for i, b in enumerate(data):
        if b == 0x04:
            op04_positions.append(i)
        elif b == 0x32:
            op32_positions.append(i)

    # ID collection after 0x04
    for pos in op04_positions:
        if pos + 2 < n:
            low = data[pos + 1]
            high = data[pos + 2]
            id_val = low | (high << 8)  # little-endian
            id_counter[id_val] += 1

    # Header pattern after 0x32 (next 4 bytes; if fewer than 4 remain skip)
    for pos in op32_positions:
        if pos + 5 <= n:
            header = data[pos + 1: pos + 5]
            header_counter[header] += 1

    # Distance from 0x32 to next 0x04 (forward scan)
    # Merge positions list for efficient next search
    op04_iter = iter(sorted(op04_positions))
    try:
        current04 = next(op04_iter)
    except StopIteration:
        current04 = None
    op04_queue = sorted(op04_positions)  # for binary search alternative
    import bisect
    for pos in op32_positions:
        idx = bisect.bisect_left(op04_queue, pos + 1)
        if idx < len(op04_queue):
            d = op04_queue[idx] - pos
            if d <= max_distance:
                distances[bucket_distance(d)] += 1
        # else no following 0x04

    # Sampling contexts
    rng = random.Random(0xC0DE)
    sample_op04 = rng.sample(op04_positions, min(sample, len(op04_positions))) if op04_positions else []
    sample_op32 = rng.sample(op32_positions, min(sample, len(op32_positions))) if op32_positions else []

    def format_counter(counter: Counter, limit: int = 25, key_fmt=None):
        items = counter.most_common(limit)
        lines = []
        for k, c in items:
            if isinstance(k, bytes):
                lines.append(f"{k.hex()}  {c}")
            else:
                if key_fmt:
                    lines.append(f"{key_fmt(k)}  {c}")
                else:
                    lines.append(f"{k}  {c}")
        return '\n'.join(lines)

    print(f"File: {path}")
    print(f"Size: {n} bytes")
    print()
    print(f"Total 0x04 occurrences: {len(op04_positions)}")
    print(f"Total 0x32 occurrences: {len(op32_positions)}")
    print()
    print("Top IDs after 0x04 (little-endian low,high) :")
    print(format_counter(id_counter, 30, key_fmt=lambda k: f"0x{k:04x}"))
    print()
    print("Top header 4-byte patterns after 0x32 :")
    print(format_counter(header_counter, 30))
    print()
    print("Distance buckets (0x32 -> next 0x04, capped):")
    for bucket, cnt in distances.most_common():
        print(f"  {bucket:>7}: {cnt}")
    print()
    print("Sample contexts around 0x04 (showing ID bytes):")
    for pos in sorted(sample_op04[:10]):
        print(f"- 0x04 @ {pos:06x}\n{hexdump_slice(data, pos)}")
    print()
    print("Sample contexts around 0x32 (showing header bytes):")
    for pos in sorted(sample_op32[:10]):
        print(f"- 0x32 @ {pos:06x}\n{hexdump_slice(data, pos)}")
    print()
    print("NOTE: +5 continuation hypothesis pending – inspect header pattern variability and distances to refine.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('file', help='Binary script file to scan')
    ap.add_argument('--sample', type=int, default=20, help='Sample size for context snippets of each opcode')
    ap.add_argument('--max-distance', type=int, default=4096, help='Cap distance histogram consideration')
    args = ap.parse_args()
    if not os.path.isfile(args.file):
        print(f"File not found: {args.file}", file=sys.stderr)
        sys.exit(1)
    analyze(args.file, args.sample, args.max_distance)


if __name__ == '__main__':
    main()
import argparse
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
import json

"""
Scan a script binary for structural marker relationships:
  - 0x32 treated as BLOCK_BEGIN (push) with continuation heuristic (+5) for pairing only.
  - 0x04 treated as BLOCK_END (pop) per FUN_0025bc68 structural interpreter.
  - After each 0x04, capture the next two bytes as a potential 16-bit ID (little-endian) if present.

Outputs summary statistics and (optionally) JSON with per-block records.
This is a heuristic pass; the +5 continuation assumption is not enforced here for traversal logic—
we only need matching order of pushes/pops to study correlations.
"""


@dataclass
class BlockRecord:
    start_pos: int          # position of 0x32 opcode
    end_pos: int            # position of matching 0x04 opcode
    id_after_end: int | None  # 16-bit little-endian value following 0x04 (if available)
    header_bytes: bytes     # bytes at start_pos+1 .. start_pos+4 (4 bytes after 0x32)


def scan(data: bytes, collect_limit: int | None = None):
    stack: list[int] = []
    blocks: list[BlockRecord] = []
    id_counter = Counter()
    total_32 = 0
    total_04 = 0
    unmatched_end = 0
    i = 0
    size = len(data)
    # Linear pass; we do not emulate relative jumps here—goal is correlation only.
    while i < size:
        b = data[i]
        if b == 0x32:
            total_32 += 1
            stack.append(i)
            i += 1
            continue
        if b == 0x04:
            total_04 += 1
            # potential ID extraction
            id_val = None
            if i + 2 < size:
                id_val = data[i+1] | (data[i+2] << 8)
                id_counter[id_val] += 1
            if stack:
                start = stack.pop()
                header = data[start+1:start+5] if start + 5 <= size else b""
                blocks.append(BlockRecord(start, i, id_val, header))
            else:
                unmatched_end += 1
            i += 1
            continue
        i += 1
    # Anything left on stack counts as unterminated blocks
    unterminated = len(stack)
    return {
        "total_bytes": size,
        "count_0x32": total_32,
        "count_0x04": total_04,
        "matched_blocks": len(blocks),
        "unmatched_end": unmatched_end,
        "unterminated_starts": unterminated,
        "distinct_ids_after_0x04": len(id_counter),
        "top_ids": id_counter.most_common(25),
        "block_sample": [
            {
                "start": br.start_pos,
                "end": br.end_pos,
                "len": br.end_pos - br.start_pos + 1,
                "id": br.id_after_end,
                "header_hex": br.header_bytes.hex()
            }
            for br in blocks[: min(collect_limit or 50, len(blocks))]
        ],
        "header_patterns_top": Counter(br.header_bytes for br in blocks).most_common(20),
    }, blocks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file", nargs="?", default="SCR.BIN", help="Script binary to scan")
    ap.add_argument("--json-out", default=None, help="Write full block list JSON to path")
    ap.add_argument("--sample", type=int, default=50, help="How many block records to include in summary sample")
    args = ap.parse_args()
    path = Path(args.file)
    data = path.read_bytes()
    summary, blocks = scan(data, collect_limit=args.sample)
    # Print human summary
    print("== Structure Marker Scan Summary ==")
    for k in [
        "total_bytes","count_0x32","count_0x04","matched_blocks","unmatched_end",
        "unterminated_starts","distinct_ids_after_0x04"]:
        print(f"{k}: {summary[k]}")
    print("\nTop IDs (after 0x04):")
    for val, cnt in summary["top_ids"]:
        print(f"  {val:5d} (0x{val:04x}) : {cnt}")
    print("\nHeader pattern frequency (first 20):")
    for hdr, cnt in summary["header_patterns_top"]:
        print(f"  {hdr.hex():>8} : {cnt}")
    print("\nSample block records:")
    for rec in summary["block_sample"]:
        print(f"  start=0x{rec['start']:06x} end=0x{rec['end']:06x} len={rec['len']:5d} id=0x{(rec['id'] or -1):04x} header={rec['header_hex']}")
    if args.json_out:
        out = {
            "summary": {k: summary[k] for k in summary if k not in ("block_sample","top_ids","header_patterns_top")},
            "top_ids": summary["top_ids"],
            "header_patterns_top": [(h.hex(), c) for h,c in summary["header_patterns_top"]],
            "blocks": blocks and [
                {
                    "start": b.start_pos,
                    "end": b.end_pos,
                    "id": b.id_after_end,
                    "header_hex": b.header_bytes.hex(),
                }
                for b in blocks
            ]
        }
        Path(args.json_out).write_text(json.dumps(out, indent=2))
        print(f"\nWrote JSON to {args.json_out}")


if __name__ == "__main__":
    main()
