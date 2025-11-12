#!/usr/bin/env python3
"""
Create a filtered OBJ that excludes any face referencing a set of hub vertices.
- Keeps original vertex list (indices preserved) so remaining faces stay valid.
- Writes a new OBJ with suffix and a comment listing dropped vertex IDs and face count delta.
"""
import argparse, os, re

def filter_obj(src, dst, drop_vertices):
    drop_set = set(drop_vertices)
    with open(src, 'r', encoding='utf-8', errors='ignore') as f:
        lines = f.readlines()
    rx_face = re.compile(r'^f\s+(.*)$')
    out_lines = []
    face_total = 0
    face_kept = 0
    dropped_faces = 0
    for line in lines:
        if line.startswith('v '):
            out_lines.append(line)
            continue
        if not line.startswith('f '):
            out_lines.append(line)
            continue
        face_total += 1
        m = rx_face.match(line)
        if not m:
            continue
        tokens = m.group(1).strip().split()
        v_idx = []
        for t in tokens:
            part = t.split('/')[0]
            try:
                vi = int(part)
            except ValueError:
                vi = None
            if vi is not None and vi < 0:
                # negative indices: convert relative (OBJ spec)
                # Count vertices so far (we can't know final count easily; rely on original list ordering)
                # Simplify: leave negative indices untouched (unlikely in our export)
                pass
            v_idx.append(vi)
        if any((vi in drop_set) for vi in v_idx if vi is not None):
            dropped_faces += 1
            continue
        out_lines.append(line)
        face_kept += 1
    header = f"# Filtered: dropped vertices={sorted(drop_set)} faces_kept={face_kept} faces_dropped={dropped_faces}/{face_total}\n"
    out_lines.insert(0, header)
    with open(dst, 'w', encoding='utf-8') as f:
        f.writelines(out_lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description='Filter OBJ by dropping faces referencing specified vertex indices')
    ap.add_argument('src', help='source OBJ path')
    ap.add_argument('dst', help='destination OBJ path')
    ap.add_argument('--drop', required=True, help='comma-separated list of vertex indices to drop faces for (e.g., 1,7,26)')
    args = ap.parse_args(argv)
    drop_vertices = [int(x) for x in args.drop.split(',') if x.strip()]
    filter_obj(args.src, args.dst, drop_vertices)
    print(f"wrote {args.dst}")
    return 0

if __name__ == '__main__':
    import sys
    raise SystemExit(main(sys.argv[1:]))
