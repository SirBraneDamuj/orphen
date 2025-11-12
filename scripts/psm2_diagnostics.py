#!/usr/bin/env python3
"""PSM2 diagnostics / exploratory tooling.

Goals:
 1. Fractional histogram of Section C vertex components (to confirm grid alignment like *.0 / *.5).
 2. Cluster D-record faces by their a_index (selector at +24 of raw D stream) and output per-cluster OBJ files
    without trying to bind through J. This can reveal whether each cluster forms a coherent local shape versus
    the global jumble.
 3. Report basic stats: vertex reuse, average face area heuristic (using raw coords), AABB per cluster.

NOTE: This does NOT attempt to apply any unknown world transforms. If clusters still look like noise, the
missing transform likely resides in a later stage (e.g., matrix setup outside FUN_00211230).
"""
from __future__ import annotations
import argparse, os, math, json, collections
from typing import List, Tuple, Dict, Set
from psm2_parser import parse_psm2, MAGIC_PSM2, _u32


def find_psm2_offsets(buf: bytes) -> list[int]:
    magic = MAGIC_PSM2.to_bytes(4, 'little')
    offs = []
    pos = 0
    while True:
        i = buf.find(magic, pos)
        if i == -1: break
        if _u32(buf, i) == MAGIC_PSM2:
            offs.append(i)
        pos = i + 1
    return offs


def frac_bucket(v: float) -> str:
    frac = abs(v - math.floor(v))
    # snap to common fractions
    for target in (0.0, 0.25, 0.5, 0.75):
        if abs(frac - target) < 1e-4:
            return f"{target:.2f}"
    return "other"


def histogram_c(summary) -> Dict[str,int]:
    buckets = collections.Counter()
    for cr in summary.c_records:
        for comp in (cr.f0, cr.f1, cr.f2):
            buckets[frac_bucket(comp)] += 1
    return dict(buckets)


def export_clusters(summary, out_dir: str, base_name: str, verbose: bool=False) -> List[str]:
    os.makedirs(out_dir, exist_ok=True)
    # Cluster D-records by a_index
    clusters: Dict[int, List] = collections.defaultdict(list)
    for d in summary.d_records:
        clusters[d.a_index].append(d)
    written = []
    for a_idx, dlist in clusters.items():
        # Collect unique C indices referenced
        verts_idx = set()
        faces = []  # list of (indices tuple) where each index references an entry in 'order'
        for d in dlist:
            s0,s1,s2,s3 = d.indices
            if s2 == s3:
                faces.append((s0,s1,s2))
                verts_idx.update([s0,s1,s2])
            else:
                faces.append((s3,s0,s1))
                faces.append((s1,s2,s3))
                verts_idx.update([s0,s1,s2,s3,s3])
        order = sorted(verts_idx)
        index_remap = {g:i+1 for i,g in enumerate(order)}  # OBJ 1-based
        # Compute AABB and a simple area sum heuristic
        minv = [float('inf')]*3
        maxv = [float('-inf')]*3
        coords: Dict[int, Tuple[float,float,float]] = {}
        for g in order:
            if 0 <= g < len(summary.c_records):
                cr = summary.c_records[g]
                coords[g] = (cr.f0, cr.f1, cr.f2)
                for j,c in enumerate(coords[g]):
                    if c < minv[j]: minv[j] = c
                    if c > maxv[j]: maxv[j] = c
        def tri_area(a,b,c):
            ax,ay,az = a; bx,by,bz = b; cx,cy,cz = c
            ux,uy,uz = bx-ax, by-ay, bz-az
            vx,vy,vz = cx-ax, cy-ay, cz-az
            cross = (uy*vz - uz*vy, uz*vx - ux*vz, ux*vy - uy*vx)
            return 0.5*math.sqrt(cross[0]**2 + cross[1]**2 + cross[2]**2)
        area_sum = 0.0
        tri_count = 0
        for f in faces:
            if len(f) == 3 and all(idx in coords for idx in f):
                area_sum += tri_area(coords[f[0]], coords[f[1]], coords[f[2]])
                tri_count += 1
        outp = os.path.join(out_dir, f"{base_name}_A{a_idx:04d}.obj")
        with open(outp, 'w', encoding='utf-8') as f:
            f.write(f"# Cluster by A-index {a_idx}\n")
            f.write(f"# D-records={len(dlist)} tris={tri_count} area_sum={area_sum:.3f}\n")
            f.write(f"# AABB min=({minv[0]:.3f},{minv[1]:.3f},{minv[2]:.3f}) max=({maxv[0]:.3f},{maxv[1]:.3f},{maxv[2]:.3f})\n")
            for g in order:
                x,y,z = coords[g]
                f.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
            for ftri in faces:
                if len(ftri)==3 and all(idx in index_remap for idx in ftri):
                    f.write(f"f {index_remap[ftri[0]]} {index_remap[ftri[1]]} {index_remap[ftri[2]]}\n")
        if verbose:
            print(f"[cluster] A={a_idx} verts={len(order)} tris={tri_count} -> {os.path.basename(outp)}")
        written.append(outp)
    return written

def export_components(summary, out_dir: str, base_name: str, verbose: bool=False) -> List[str]:
    """Connected-component clustering over D faces using shared C indices.
    This ignores A-index boundaries: any faces sharing at least one vertex index
    are part of the same component. Helps separate disjoint geometry blobs.
    """
    os.makedirs(out_dir, exist_ok=True)
    # Build adjacency: vertex -> list of face ids, and face list
    faces: List[Tuple[int,int,int]] = []
    for d in summary.d_records:
        s0,s1,s2,s3 = d.indices
        if s2 == s3:
            faces.append((s0,s1,s2))
        else:
            faces.append((s3,s0,s1))
            faces.append((s1,s2,s3))
    v_to_faces: Dict[int, Set[int]] = {}
    for fi, (a,b,c) in enumerate(faces):
        for v in (a,b,c):
            v_to_faces.setdefault(v,set()).add(fi)
    # Face adjacency via shared vertex
    face_adj: List[Set[int]] = [set() for _ in faces]
    for fi,(a,b,c) in enumerate(faces):
        nbrs = (v_to_faces.get(a,set()) | v_to_faces.get(b,set()) | v_to_faces.get(c,set()))
        nbrs.discard(fi)
        face_adj[fi] = nbrs
    # DFS to collect components
    comps: List[List[int]] = []
    visited: Set[int] = set()
    for fi in range(len(faces)):
        if fi in visited: continue
        stack=[fi]; comp=[]
        while stack:
            cur=stack.pop()
            if cur in visited: continue
            visited.add(cur); comp.append(cur)
            stack.extend(face_adj[cur])
        comps.append(comp)
    written=[]
    for ci, comp_faces in enumerate(comps):
        verts_idx=set()
        for fi in comp_faces:
            a,b,c=faces[fi]; verts_idx.update((a,b,c))
        order=sorted(verts_idx)
        remap={g:i+1 for i,g in enumerate(order)}
        # Gather coords
        coords={} ; minv=[float('inf')]*3 ; maxv=[float('-inf')]*3
        for g in order:
            if 0 <= g < len(summary.c_records):
                cr=summary.c_records[g]
                coords[g]=(cr.f0,cr.f1,cr.f2)
                for j,cval in enumerate(coords[g]):
                    if cval<minv[j]: minv[j]=cval
                    if cval>maxv[j]: maxv[j]=cval
        outp=os.path.join(out_dir,f"{base_name}_COMP{ci:04d}.obj")
        with open(outp,'w',encoding='utf-8') as f:
            f.write(f"# Component {ci} faces={len(comp_faces)} verts={len(order)}\n")
            f.write(f"# AABB min=({minv[0]:.3f},{minv[1]:.3f},{minv[2]:.3f}) max=({maxv[0]:.3f},{maxv[1]:.3f},{maxv[2]:.3f})\n")
            for g in order:
                x,y,z=coords[g]; f.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
            for fi in comp_faces:
                a,b,c=faces[fi]
                if a in remap and b in remap and c in remap:
                    f.write(f"f {remap[a]} {remap[b]} {remap[c]}\n")
        written.append(outp)
        if verbose:
            print(f"[component] id={ci} faces={len(comp_faces)} verts={len(order)} -> {os.path.basename(outp)}")
    return written


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description='PSM2 diagnostics (fraction histogram + A-index clusters)')
    ap.add_argument('input', help='PSM2 file or container path')
    ap.add_argument('--offset', type=lambda s:int(s,0), default=0, help='offset of magic (0x..)')
    ap.add_argument('--outdir', default='out/psm2_diag', help='output directory for cluster OBJs')
    ap.add_argument('--verbose', action='store_true')
    args = ap.parse_args(argv)
    data = open(args.input,'rb').read()
    if args.offset:
        if args.offset >= len(data): raise SystemExit('offset out of range')
        data = data[args.offset:]
    summary = parse_psm2(data)
    frac_hist = histogram_c(summary)
    print(json.dumps({'fraction_histogram': frac_hist, 'c_count': len(summary.c_records), 'd_count': len(summary.d_records), 'j_count': len(summary.j_records)}, indent=2))
    base = os.path.splitext(os.path.basename(args.input))[0]
    # A-index clusters
    export_clusters(summary, os.path.join(args.outdir,'clusters'), base, verbose=args.verbose)
    # Connected components
    export_components(summary, os.path.join(args.outdir,'components'), base, verbose=args.verbose)
    return 0

if __name__ == '__main__':
    import sys
    raise SystemExit(main(sys.argv[1:]))
