#!/usr/bin/env python3
"""
Analyze OBJ for vertex hubs (vertices referenced by many faces):
- Computes face usage per vertex (degree).
- For top-K hubs, reports:
  * coordinates
  * unique neighbor count
  * top-5 neighbor vertices by co-incident edges
  * edge length stats from hub to neighbors (min/median,p95,max)
- Optionally writes a JSON report.
"""
import argparse, json, os, re, math
from collections import Counter, defaultdict

def parse_obj(path):
    verts = []  # 1-based indexing: pad with dummy at index 0
    verts.append((0.0,0.0,0.0))
    faces = []  # list of (a,b,c) vertex indices (1-based)
    rx_face = re.compile(r"^f\s+(.*)$")
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            if not line:
                continue
            if line.startswith('v '):
                parts = line.split()
                if len(parts) >= 4:
                    try:
                        x = float(parts[1]); y = float(parts[2]); z = float(parts[3])
                    except ValueError:
                        continue
                    verts.append((x,y,z))
                continue
            if not line.startswith('f '):
                continue
            m = rx_face.match(line)
            if not m:
                continue
            tokens = m.group(1).strip().split()
            if len(tokens) < 3:
                continue
            idxs = []
            for tok in tokens:
                part = tok.split('/')[0]
                try:
                    vi = int(part)
                except ValueError:
                    continue
                if vi < 0:
                    vi = len(verts) + vi
                idxs.append(vi)
            if len(idxs) == 3:
                faces.append((idxs[0], idxs[1], idxs[2]))
            else:
                for k in range(1, len(idxs)-1):
                    faces.append((idxs[0], idxs[k], idxs[k+1]))
    return verts, faces


def hub_report(verts, faces, top):
    usage = Counter()
    neighbors = defaultdict(Counter)
    for a,b,c in faces:
        usage[a] += 1; usage[b] += 1; usage[c] += 1
        neighbors[a].update([b,c])
        neighbors[b].update([a,c])
        neighbors[c].update([a,b])
    total_faces = len(faces)
    total_verts = len(verts) - 1
    top_vs = [vi for vi,_ in usage.most_common(top)]
    def dist(i,j):
        ax,ay,az = verts[i]; bx,by,bz = verts[j]
        return math.sqrt((ax-bx)**2 + (ay-by)**2 + (az-bz)**2)
    hubs = []
    for vi in top_vs:
        coord = verts[vi]
        neigh = neighbors[vi]
        uniq = len(neigh)
        # edge lengths to neighbors (counted with multiplicity):
        lengths = []
        for nj, cnt in neigh.items():
            d = dist(vi, nj)
            lengths.extend([d]*cnt)
        lengths.sort()
        def pct(arr, q):
            if not arr: return None
            i = int(q*(len(arr)-1))
            return arr[i]
        top_neigh = neigh.most_common(5)
        hubs.append({
            'v': vi,
            'coord': coord,
            'refs': usage[vi],
            'refs_frac': usage[vi]/total_faces if total_faces else 0,
            'unique_neighbors': uniq,
            'top_neighbors': [{'v': j, 'refs': c, 'dist': dist(vi,j)} for j,c in top_neigh],
            'edge_length': {
                'min': lengths[0] if lengths else None,
                'median': pct(lengths, 0.5),
                'p95': pct(lengths, 0.95),
                'max': lengths[-1] if lengths else None,
            }
        })
    return {
        'vertices': total_verts,
        'faces': total_faces,
        'top': hubs
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description='OBJ vertex hub analyzer')
    ap.add_argument('obj', help='path to OBJ file')
    ap.add_argument('--top', type=int, default=20, help='analyze top-K vertices by face references')
    ap.add_argument('--json', help='optional output JSON path')
    args = ap.parse_args(argv)
    verts, faces = parse_obj(args.obj)
    rep = hub_report(verts, faces, args.top)
    print(f"OBJ: {os.path.basename(args.obj)} vertices={rep['vertices']} faces={rep['faces']}")
    for h in rep['top']:
        x,y,z = h['coord']
        em = h['edge_length']
        def fmt(v):
            return f"{v:.3f}" if v is not None else "None"
        print(f"v[{h['v']}] refs={h['refs']} ({h['refs_frac']:.2%}) coord=({x:.3f},{y:.3f},{z:.3f}) uniq_neighbors={h['unique_neighbors']}\n"
              f"  edge_len min/med/p95/max = {fmt(em['min'])}/{fmt(em['median'])}/{fmt(em['p95'])}/{fmt(em['max'])}")
        print("  top neighbors:")
        for tn in h['top_neighbors']:
            print(f"    -> v[{tn['v']}] refs_with_hub={tn['refs']} dist={tn['dist']:.3f}")
    if args.json:
        with open(args.json, 'w', encoding='utf-8') as jf:
            json.dump(rep, jf, indent=2)
        print(f"wrote {args.json}")
    return 0

if __name__ == '__main__':
    import sys
    raise SystemExit(main(sys.argv[1:]))
