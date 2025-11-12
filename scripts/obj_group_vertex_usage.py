#!/usr/bin/env python3
"""
Compute per-group (e.g., grouped-by-A global OBJ) vertex usage histograms.
Assumes an OBJ where groups (g a_XXX) share the same unified vertex list.
For each group, counts how many faces reference each vertex and reports top-N hubs.
"""
import argparse, re, json, os

def parse_obj(path):
    verts = []
    groups = {}
    cur = None
    with open(path, 'r', encoding='utf-8') as f:
        for line in f:
            if line.startswith('v '):
                verts.append(line.strip())
            elif line.startswith('g '):
                cur = line.strip().split(' ',1)[1]
                groups.setdefault(cur, [])
            elif line.startswith('f '):
                if cur is None:
                    cur = 'default'
                    groups.setdefault(cur, [])
                parts = line.strip().split()[1:]
                idxs = []
                for p in parts:
                    # handle v//vn or v/vt/vn syntaxes
                    vpart = p.split('/')[0]
                    try:
                        idxs.append(int(vpart))
                    except ValueError:
                        pass
                if len(idxs) == 3:
                    groups[cur].append(tuple(idxs))
    return verts, groups

def main(argv=None):
    ap = argparse.ArgumentParser(description='Per-group vertex usage histogram')
    ap.add_argument('obj', help='grouped OBJ (e.g., *_global_byA.obj)')
    ap.add_argument('--top', type=int, default=10, help='report top-N hubs per group')
    ap.add_argument('--out', help='optional JSON output path')
    args = ap.parse_args(argv)
    verts, groups = parse_obj(args.obj)
    result = {'file': args.obj, 'groups': []}
    for gname, faces in groups.items():
        usage = {}
        for a,b,c in faces:
            usage[a] = usage.get(a,0)+1
            usage[b] = usage.get(b,0)+1
            usage[c] = usage.get(c,0)+1
        if not usage:
            continue
        total_refs = sum(usage.values())
        # sort by count desc
        top_items = sorted(usage.items(), key=lambda kv: kv[1], reverse=True)[:args.top]
        result['groups'].append({
            'group': gname,
            'faces': len(faces),
            'unique_vertices': len(usage),
            'top_vertices': [{'index': idx, 'refs': cnt, 'percent_of_group_refs': cnt/total_refs} for idx,cnt in top_items]
        })
    # simple text output
    for g in result['groups']:
        print(f"Group {g['group']}: faces={g['faces']} unique_v={g['unique_vertices']}")
        for tv in g['top_vertices']:
            print(f"  v[{tv['index']}] refs={tv['refs']} pct={tv['percent_of_group_refs']:.3f}")
    if args.out:
        with open(args.out,'w',encoding='utf-8') as f:
            json.dump(result, f, indent=2)
        print(f"[json] wrote {args.out}")
    return 0

if __name__ == '__main__':
    import sys
    raise SystemExit(main(sys.argv[1:]))
