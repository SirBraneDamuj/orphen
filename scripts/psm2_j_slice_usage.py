#!/usr/bin/env python3
"""
Per-J slice usage: for each J record, count faces whose all four D indices fall within the J's contiguous C slice.
Reports top J by face count and, optionally, usage of specified hub indices within those faces.
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
    ap = argparse.ArgumentParser(description='PSM2 per-J slice face counts and hub usage')
    ap.add_argument('input', help='path to .psm2 or container')
    ap.add_argument('--offset', type=lambda s:int(s,0), help='optional offset to PSM2 magic')
    ap.add_argument('--indices', type=int, nargs='*', default=[1,7,26,36,21], help='C indices to track across J slices')
    ap.add_argument('--out', help='optional JSON output')
    args = ap.parse_args(argv)
    summary = load_first_psm2(args.input, args.offset)
    out = {
        'file': args.input,
        'j_records': []
    }
    for j_i, jr in enumerate(summary.j_records):
        if jr.count <= 0 or jr.c_index < 0:
            continue
        s0 = jr.c_index
        s1 = jr.c_index + jr.count
        faces = 0
        usage = {idx: 0 for idx in args.indices}
        for d in summary.d_records:
            a,b,c,d3 = d.indices
            if not (s0 <= a < s1 and s0 <= b < s1 and s0 <= c < s1 and s0 <= d3 < s1):
                continue
            if c == d3:
                faces += 1
                for idx in args.indices:
                    if idx in (a,b,c):
                        usage[idx] += 1
            else:
                faces += 2
                for tri in ((d3,a,b),(b,c,d3)):
                    for idx in args.indices:
                        if idx in tri:
                            usage[idx] += 1
        if faces:
            out['j_records'].append({
                'j_index': j_i,
                'a_index': jr.a_index,
                'c_start': jr.c_index,
                'count': jr.count,
                'faces': faces,
                'tracked_index_usage': usage
            })
    out['j_records'].sort(key=lambda x: x['faces'], reverse=True)
    print(json.dumps(out, indent=2))
    if args.out:
        with open(args.out,'w',encoding='utf-8') as f:
            json.dump(out,f,indent=2)
            print(f"[json] wrote {args.out}")
    return 0

if __name__ == '__main__':
    import sys
    raise SystemExit(main(sys.argv[1:]))
