#!/usr/bin/env python3
"""
A-section histogram and per-A face stats from a PSM2 file.
Reports:
- Top A records by face count (from D-records' a_index with tri/quad weighting)
- Histogram of A.short0 (C-slice starts) and A.short1 (counts)
- Basic summary: totals for A, C, D
"""
import argparse, os, json
from psm2_parser import parse_psm2, MAGIC_PSM2

def find_psm2_offsets(buf: bytes) -> list[int]:
    magic = MAGIC_PSM2.to_bytes(4, 'little')
    offs = []
    pos = 0
    while True:
        i = buf.find(magic, pos)
        if i == -1:
            break
        if buf[i:i+4] == magic:
            offs.append(i)
        pos = i + 1
    return offs


def load_first_psm2(path: str, offset: int | None):
    data = open(path, 'rb').read()
    if offset is not None:
        return parse_psm2(data[offset:])
    if len(data) >= 4 and data[:4] == MAGIC_PSM2.to_bytes(4,'little'):
        return parse_psm2(data)
    offs = find_psm2_offsets(data)
    if not offs:
        raise SystemExit('No PSM2 magic found')
    return parse_psm2(data[offs[0]:])


def main(argv=None):
    ap = argparse.ArgumentParser(description='PSM2 A-section histogram and per-A face stats')
    ap.add_argument('input', help='path to .psm2 or container')
    ap.add_argument('--offset', type=lambda s:int(s,0), help='optional offset to PSM2 magic')
    ap.add_argument('--out', help='optional JSON output')
    args = ap.parse_args(argv)
    summary = load_first_psm2(args.input, args.offset)
    # per-A face counts and unique vertex usage
    faces_per_a = {}
    unique_v_per_a: dict[int,set[int]] = {}
    for d in summary.d_records:
        tris = 1 if d.indices[2] == d.indices[3] else 2
        ai = d.a_index
        faces_per_a[ai] = faces_per_a.get(ai, 0) + tris
        s0,s1,s2,s3 = d.indices
        if ai not in unique_v_per_a:
            unique_v_per_a[ai] = set()
        unique_v_per_a[ai].update((s0,s1,s2))
        if s2 != s3:
            unique_v_per_a[ai].add(s3)
    # top A by faces
    top_a = sorted(faces_per_a.items(), key=lambda kv: kv[1], reverse=True)[:10]
    # histograms of short0 and short1
    from collections import Counter
    short0 = [a.short0 for a in summary.a_records]
    short1 = [a.short1 for a in summary.a_records]
    h0 = Counter(short0)
    h1 = Counter(short1)
    out = {
        'file': args.input,
        'counts': {
            'A': len(summary.a_records),
            'C': len(summary.c_records),
            'D': len(summary.d_records),
        },
        'top_a_by_faces': [
            {
                'a_index': ai,
                'faces': faces,
                'short0': summary.a_records[ai].short0 if 0 <= ai < len(summary.a_records) else None,
                'short1': summary.a_records[ai].short1 if 0 <= ai < len(summary.a_records) else None,
                'unique_vertices': len(unique_v_per_a.get(ai, set()))
            }
            for ai, faces in top_a
        ],
        'short0_hist_top': h0.most_common(20),
        'short1_hist_top': h1.most_common(20),
    }
    print(json.dumps(out, indent=2))
    if args.out:
        with open(args.out, 'w', encoding='utf-8') as f:
            json.dump(out, f, indent=2)
            print(f"[json] wrote {args.out}")
    return 0

if __name__ == '__main__':
    import sys
    raise SystemExit(main(sys.argv[1:]))
