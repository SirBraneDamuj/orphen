#!/usr/bin/env python3
"""Scan a decompressed sector for plausible pointer tables to text/script blocks.

Heuristics:
  * Look for runs of at least MIN_COUNT little-endian 32-bit offsets located within file size.
  * Offsets must be strictly ascending (or non-decreasing) and within a window (e.g. all offsets < file_size and > 0).
  * Table bytes themselves should largely lie before first pointed-to target (typical layout: table header then data blobs).
  * Optionally attempt 16-bit pointer tables (relative *2) if needed (disabled by default).
  * For each candidate table, attempt to extract ASCII-like strings (>=3 printable chars) at each pointer until next pointer target (or NUL) to validate.

Outputs summary of candidates with sample reconstructed strings.
"""
import argparse, pathlib, string
from typing import List, Tuple

PRINTABLE = set(bytes(string.printable,'ascii')) - set(b"\x0b\x0c\x0b")

def is_text_block(b: bytes) -> bool:
    if len(b) < 3:
        return False
    letters = sum((c>=0x41 and c<=0x7a) for c in b)
    ratio = letters / len(b)
    return ratio > 0.4

def extract_string(data: bytes, off: int, limit: int) -> str:
    out=[]
    i=off
    while i < limit and i < len(data):
        c = data[i]
        if c == 0 or c not in PRINTABLE:
            break
        out.append(chr(c))
        i+=1
    return ''.join(out)

def scan_32(data: bytes, start: int, end: int, min_count=5, max_gap=4):
    size = len(data)
    candidates=[]
    i=start
    while i+4*min_count <= end and i+4 <= size:
        vals=[]
        j=i
        last=-1
        while j+4 <= size:
            v=int.from_bytes(data[j:j+4],'little')
            if not (0 < v < size):
                break
            if last != -1 and v < last:
                break
            vals.append(v)
            last=v
            j+=4
            # stop if gap between table bytes and target too big to be header (heuristic)
            if len(vals)>=min_count:
                # If first target < i, unlikely pointer table where table precedes data.
                if vals[0] < i:
                    break
                # If we've accumulated enough and next value jumps backwards, break
        if len(vals) >= min_count:
            candidates.append((i, len(vals), vals))
            i = j  # skip past this table
        else:
            i += 4
    return candidates

def analyze_candidates(data: bytes, cands: List[Tuple[int,int,List[int]]], sample=5):
    results=[]
    for off,count,vals in cands:
        samples=[]
        for idx,v in enumerate(vals[:sample]):
            nxt = vals[idx+1] if idx+1 < len(vals) else len(data)
            s = extract_string(data, v, nxt)
            if s:
                samples.append(s)
        plausible = sum(is_text_block(s.encode('ascii','ignore')) for s in samples) >= 2
        results.append({'table_offset':off,'entries':count,'first_target':vals[0],'samples':samples,'plausible_text':plausible})
    return results

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--file', required=True)
    ap.add_argument('--start', type=lambda x:int(x,0), default=0)
    ap.add_argument('--end', type=lambda x:int(x,0))
    ap.add_argument('--min-count', type=int, default=6)
    ap.add_argument('--json', action='store_true')
    args=ap.parse_args()
    data=pathlib.Path(args.file).read_bytes()
    end = min(len(data), args.end) if args.end else len(data)
    cands = scan_32(data, args.start, end, min_count=args.min_count)
    analyzed = analyze_candidates(data, cands)
    if args.json:
        import json
        json.dump({'candidates':analyzed}, open(1,'w'), indent=2)
    else:
        for r in analyzed:
            print(f"table@0x{r['table_offset']:06x} entries~{r['entries']:4d} first_target=0x{r['first_target']:06x} plausible={r['plausible_text']}")
            for s in r['samples']:
                print(f"   sample: {s[:80]}")

if __name__=='__main__':
    main()
