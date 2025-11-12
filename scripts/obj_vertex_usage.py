#!/usr/bin/env python3
"""
Compute a histogram of how many faces reference each vertex in an OBJ file.
- Counts per OBJ "v" index (1-based) across all "f" statements.
- Prints summary stats and top-K most-referenced vertices.
- Optionally, writes a JSON file with full histogram.
"""
import argparse, json, os, re
from collections import Counter

def parse_obj_vertex_usage(path: str):
    v_count = 0
    usage = Counter()
    face_count = 0
    # f tokens can be v, v//n, v/t, v/t/n; we only care about the first integer before any slash
    rx_face = re.compile(r"^f\s+(.*)$")
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            if not line:
                continue
            c = line[0]
            if c == 'v' and (len(line) == 1 or line[1].isspace()):
                v_count += 1
                continue
            if c != 'f':
                continue
            m = rx_face.match(line)
            if not m:
                continue
            tokens = m.group(1).strip().split()
            if len(tokens) < 3:
                continue
            idxs = []
            for tok in tokens:
                # split on '/'; take first part as vertex index
                part = tok.split('/')[0]
                try:
                    vi = int(part)
                except ValueError:
                    continue
                if vi > 0:
                    idxs.append(vi)
                else:
                    # negative indices are relative to current v_count
                    idxs.append(v_count + vi + 1)
            # triangulate n-gons fanwise: (0,1,2), (0,2,3), ...
            if len(idxs) == 3:
                tri = [idxs]
            else:
                tri = []
                for k in range(1, len(idxs)-1):
                    tri.append([idxs[0], idxs[k], idxs[k+1]])
            for a,b,c in tri:
                usage[a] += 1
                usage[b] += 1
                usage[c] += 1
                face_count += 1
    return v_count, face_count, usage


def main(argv=None):
    ap = argparse.ArgumentParser(description='OBJ vertex usage histogram (faces per vertex)')
    ap.add_argument('obj', help='path to OBJ file')
    ap.add_argument('--json', help='optional output JSON path for full histogram')
    ap.add_argument('--top', type=int, default=20, help='print top-K most-referenced vertices')
    args = ap.parse_args(argv)
    v_count, face_count, usage = parse_obj_vertex_usage(args.obj)
    if v_count == 0:
        print(json.dumps({'error': 'no vertices found'}))
        return 1
    # stats
    total_refs = sum(usage.values())
    avg_refs = total_refs / v_count if v_count else 0
    max_refs = max(usage.values()) if usage else 0
    # build histogram of counts -> how many vertices have that count
    hist = Counter(usage.values())
    print(f"OBJ: {os.path.basename(args.obj)}")
    print(f"vertices={v_count} faces={face_count}")
    print(f"avg_face_refs_per_vertex={avg_refs:.2f} max_refs={max_refs}")
    print("count -> vertices:")
    for count in sorted(hist.keys()):
        print(f"  {count}: {hist[count]}")
    # top-K
    print(f"\nTop {min(args.top, len(usage))} vertices by face refs:")
    for vi, cnt in sorted(usage.items(), key=lambda kv: kv[1], reverse=True)[:args.top]:
        print(f"  v[{vi}] -> {cnt}")
    # optional JSON
    if args.json:
        with open(args.json, 'w', encoding='utf-8') as jf:
            json.dump({
                'vertices': v_count,
                'faces': face_count,
                'avg_face_refs_per_vertex': avg_refs,
                'max_refs': max_refs,
                'histogram': dict(hist),
                'top': sorted([{ 'v': vi, 'refs': cnt } for vi,cnt in usage.items()], key=lambda x: x['refs'], reverse=True)[:args.top]
            }, jf, indent=2)
        print(f"wrote {args.json}")
    return 0

if __name__ == '__main__':
    import sys
    raise SystemExit(main(sys.argv[1:]))
