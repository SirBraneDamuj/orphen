#!/usr/bin/env python3
"""
D-index usage: for specified C vertex indices, count how many faces reference them and in which A groups.
Weights by triangles (quads -> 2). Useful to see if early indices (e.g., 1,7,26) are shared across many A slices.
"""
import argparse, json
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
    ap = argparse.ArgumentParser(description='PSM2 D-index usage by A-index buckets')
    ap.add_argument('input', help='path to .psm2 or container')
    ap.add_argument('--offset', type=lambda s:int(s,0), help='optional offset to PSM2 magic')
    ap.add_argument('--indices', type=int, nargs='+', required=True, help='C indices to inspect (1-based in OBJ are 0-based here)')
    ap.add_argument('--out', help='optional JSON output')
    args = ap.parse_args(argv)
    summary = load_first_psm2(args.input, args.offset)
    target = set(args.indices)
    # per-target: total faces and per-A breakdown
    res = {}
    for idx in target:
        res[idx] = {'total_faces': 0, 'per_a': {}}
    for d in summary.d_records:
        s0,s1,s2,s3 = d.indices
        verts = (s0,s1,s2,s3)
        tris = 1 if s2 == s3 else 2
        if any(v in target for v in verts):
            for idx in target:
                if idx in verts:
                    res[idx]['total_faces'] += tris
                    res[idx]['per_a'][d.a_index] = res[idx]['per_a'].get(d.a_index, 0) + tris
    # format output
    out = {'file': args.input, 'results': []}
    for idx, info in res.items():
        per_a_sorted = sorted(info['per_a'].items(), key=lambda kv: kv[1], reverse=True)
        out['results'].append({
            'index': idx,
            'total_faces': info['total_faces'],
            'top_a': per_a_sorted[:10]
        })
    print(json.dumps(out, indent=2))
    if args.out:
        with open(args.out, 'w', encoding='utf-8') as f:
            json.dump(out, f, indent=2)
            print(f"[json] wrote {args.out}")
    return 0

if __name__ == '__main__':
    import sys
    raise SystemExit(main(sys.argv[1:]))
