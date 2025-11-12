#!/usr/bin/env python3
"""
Export PSM2 geometry slices (per J-record) to OBJ, with provisional face mapping.

Vertices:
    For each J-record we reconstruct an absolute vertex list from its owning A-record:
        - Start = A.short0 (index into Section C triplet domain)
        - Count = A.short1
        - Vertices = C[start .. start+count) triplets, followed by the appended A.extra_xyz triplet.
    (Runtime constructs a differential buffer; we expose absolute positions used by normals/AABB.)

Faces (provisional; DO NOT treat as authoritative mesh topology):
    D-record indices reference Section C directly for plane/AABB computation. We emit faces only when
    all four indices fall strictly within the contiguous C slice of the J-record (S in [start, start+count)).
    Triangle if S2 == S3; else two triangles with ordering (S3,S0,S1) and (S1,S2,S3) per code.
    The appended extra A triplet is not referenced by D indices; we do not map global index 0 specially.

Limitations:
    - Some runtime packets can fetch vertex positions from Section B via a flag-controlled indirection; this
        exporter ignores that alternate source pending flag decoding.
    - J differential encoding is not reproduced; exported vertices are absolute.
    - Faces whose indices partially overlap a J slice are skipped; mixed-source primitives are not emitted.
"""
from __future__ import annotations
import argparse
import os
import re
from typing import Optional
from psm2_parser import parse_psm2, MAGIC_PSM2, _u32


def find_psm2_offsets(buf: bytes) -> list[int]:
    magic = MAGIC_PSM2.to_bytes(4, 'little')
    offs = []
    pos = 0
    while True:
        i = buf.find(magic, pos)
        if i == -1:
            break
        if _u32(buf, i) == MAGIC_PSM2:
            offs.append(i)
        pos = i + 1
    return offs


def export_psm2(path: str, dst_dir: str, verbose: bool=False, faces_mode: str = 'slice', alt_b: bool=False, vn_mode: str = 'off',
                components: bool = False, dual_sources: bool = False, group_by_a: bool = False,
                quad_diag: str = 'fixed') -> list[str]:
    data = open(path, 'rb').read()
    offsets = [0] if (len(data) >= 4 and _u32(data, 0) == MAGIC_PSM2) else []
    offsets += [o for o in find_psm2_offsets(data) if o not in offsets]
    written: list[str] = []
    for off in offsets:
        try:
            summary = parse_psm2(data[off:])
        except Exception as e:
            if verbose:
                print(f"[skip] {path}@0x{off:X}: {e}")
            continue
        os.makedirs(dst_dir, exist_ok=True)
        base = os.path.splitext(os.path.basename(path))[0]
        for i, jr in enumerate(summary.j_records):
            if not jr.vertices:
                continue
            outp = os.path.join(dst_dir, f"{base}_ofs{off}_rec{i:03d}.obj")
            with open(outp, 'w', encoding='utf-8') as f:
                f.write(f"# PSM2 vertices from {os.path.basename(path)} @0x{off:X} J-record {i}\n")
                f.write(f"# A.index={jr.a_index} C.start={jr.c_index} count={jr.count} verts={len(jr.vertices)}\n")
                f.write(f"g rec_{i:03d}\n")
                for (x,y,z) in jr.vertices:
                    f.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
                faces_written = 0
                if faces_mode == 'slice':
                    # Emit faces whose indices exclusively reside inside the contiguous C slice
                    slice_start = jr.c_index
                    slice_end = jr.c_index + jr.count  # exclusive
                    for d in summary.d_records:
                        s0, s1, s2, s3 = d.indices
                        # Require all referenced indices inside slice
                        if not (slice_start <= s0 < slice_end and slice_start <= s1 < slice_end and
                                slice_start <= s2 < slice_end and slice_start <= s3 < slice_end):
                            continue
                        # Local OBJ indices (1-based). Appended extra vertex is never referenced by D indices.
                        def to_local(ix: int) -> int:
                            return (ix - slice_start) + 1
                        if s2 == s3:  # triangle
                            f.write(f"f {to_local(s0)} {to_local(s1)} {to_local(s2)}\n")
                            faces_written += 1
                        else:  # quad split
                            f.write(f"f {to_local(s3)} {to_local(s0)} {to_local(s1)}\n")
                            f.write(f"f {to_local(s1)} {to_local(s2)} {to_local(s3)}\n")
                            faces_written += 2
                elif faces_mode == 'off':
                    pass
                else:
                    # unknown mode per-record; handled globally below
                    pass
                if verbose:
                    print(f"[faces] rec {i:03d}: wrote {faces_written} faces ({faces_mode})")
            written.append(outp)
            if verbose:
                print(f"[wrote] {outp}")

            # Option A: also emit a Section-B substituted variant for slice mode when requested
            if faces_mode == 'slice' and dual_sources and summary.b_records and jr.count > 0:
                # Build B-based vertices for the C slice using the C->B mapping; keep the appended A triplet
                b_verts = []
                valid_map = False
                for k in range(jr.count):
                    idx = jr.c_index + k
                    if 0 <= idx < len(summary.c_records):
                        b_idx = summary.c_records[idx].b_index
                        if 0 <= b_idx < len(summary.b_records):
                            vx, vy, vz = summary.b_records[b_idx]
                            b_verts.append((vx, vy, vz))
                            valid_map = True
                        else:
                            # Fallback to C when mapping is out of range
                            vx, vy, vz = summary.c_records[idx].f0, summary.c_records[idx].f1, summary.c_records[idx].f2
                            b_verts.append((vx, vy, vz))
                    else:
                        break
                # Append the extra A triplet if present in original jr.vertices
                if len(jr.vertices) == jr.count + 1:
                    b_verts.append(jr.vertices[-1])
                outp_b = os.path.join(dst_dir, f"{base}_ofs{off}_rec{i:03d}_b.obj")
                with open(outp_b, 'w', encoding='utf-8') as f:
                    f.write(f"# PSM2 vertices (Section B substituted) from {os.path.basename(path)} @0x{off:X} J-record {i}\n")
                    f.write(f"# A.index={jr.a_index} C.start={jr.c_index} count={jr.count} verts={len(b_verts)} map_valid={int(valid_map)}\n")
                    f.write(f"g rec_{i:03d}_b\n")
                    for (x,y,z) in b_verts:
                        f.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
                    # Reuse same slice-local faces
                    slice_start = jr.c_index
                    slice_end = jr.c_index + jr.count
                    faces_written_b = 0
                    for d in summary.d_records:
                        s0, s1, s2, s3 = d.indices
                        if not (slice_start <= s0 < slice_end and slice_start <= s1 < slice_end and
                                slice_start <= s2 < slice_end and slice_start <= s3 < slice_end):
                            continue
                        def to_local(ix: int) -> int:
                            return (ix - slice_start) + 1
                        if s2 == s3:
                            f.write(f"f {to_local(s0)} {to_local(s1)} {to_local(s2)}\n")
                            faces_written_b += 1
                        else:
                            f.write(f"f {to_local(s3)} {to_local(s0)} {to_local(s1)}\n")
                            f.write(f"f {to_local(s1)} {to_local(s2)} {to_local(s3)}\n")
                            faces_written_b += 2
                written.append(outp_b)
                if verbose:
                    print(f"[wrote] {outp_b} (dual-sources B variant)")
        # Optional: global mesh using the entire C table and all D-records (avoids J-slice binding)
        if faces_mode == 'global' and summary.c_records and summary.d_records:
            outp = os.path.join(dst_dir, f"{base}_ofs{off}_global.obj")
            with open(outp, 'w', encoding='utf-8') as f:
                f.write(f"# Global C-domain mesh from {os.path.basename(path)} @0x{off:X}\n")
                f.write("g global\n")
                # Emit positions from Section C (authoritative for geometry)
                for cr in summary.c_records:
                    f.write(f"v {cr.f0:.6f} {cr.f1:.6f} {cr.f2:.6f}\n")
                # Optional vertex normals
                if vn_mode != 'off':
                    if vn_mode == 'b':
                        # Use Section B remapped vectors as normals
                        for cr in summary.c_records:
                            if 0 <= cr.b_index < len(summary.b_records):
                                nx,ny,nz = summary.b_records[cr.b_index]
                                f.write(f"vn {nx:.6f} {ny:.6f} {nz:.6f}\n")
                            else:
                                f.write("vn 0 0 1\n")
                    elif vn_mode == 'tri':
                        # Placeholder: write dummy normals; true per-vertex averaging happens below if needed
                        for _ in summary.c_records:
                            f.write("vn 0 0 1\n")
                # Assemble faces from D-records (global mesh view)
                faces = []  # list of triplets of C indices
                # Also bucket faces per A-index for grouped export/analysis
                a_buckets = {}
                # Alternative: faces with per-A base offset added (hypothesis: D indices are local to A slice)
                faces_aofs = []
                a_buckets_aofs = {}
                import math
                def pos_of(i):
                    cr = summary.c_records[i]
                    return (cr.f0,cr.f1,cr.f2)
                def tri_area(a,b,c):
                    ax,ay,az=a; bx,by,bz=b; cx,cy,cz=c
                    ux,uy,uz = bx-ax, by-ay, bz-az
                    vx,vy,vz = cx-ax, cy-ay, cz-az
                    nx = uy*vz - uz*vy
                    ny = uz*vx - ux*vz
                    nz = ux*vy - uy*vx
                    return 0.5*math.sqrt(nx*nx+ny*ny+nz*nz)
                for d in summary.d_records:
                    s0, s1, s2, s3 = d.indices
                    # Guard against out-of-range shorts
                    n = len(summary.c_records)
                    if not (0 <= s0 < n and 0 <= s1 < n and 0 <= s2 < n and 0 <= s3 < n):
                        continue
                    aidx = d.a_index
                    # Base offset from owning A-record's C slice start
                    abase = 0
                    if 0 <= aidx < len(summary.a_records):
                        abase = summary.a_records[aidx].short0
                    if s2 == s3:
                        faces.append((s0,s1,s2))
                        a_buckets.setdefault(aidx, []).append((s0,s1,s2))
                        faces_aofs.append((s0+abase, s1+abase, s2+abase))
                        a_buckets_aofs.setdefault(aidx, []).append((s0+abase, s1+abase, s2+abase))
                    else:
                        if quad_diag == 'auto':
                            # Compare areas for the two possible diagonals and choose the larger total (more stable)
                            v0,v1,v2,v3 = pos_of(s0),pos_of(s1),pos_of(s2),pos_of(s3)
                            area_031 = tri_area(v3,v0,v1) + tri_area(v1,v2,v3)  # our previous split (diag 1-3)
                            area_012 = tri_area(v0,v1,v2) + tri_area(v2,v3,v0)  # alternative split (diag 0-2)
                            if area_012 > area_031:
                                faces.append((s0,s1,s2))
                                faces.append((s2,s3,s0))
                                a_buckets.setdefault(aidx, []).append((s0,s1,s2))
                                a_buckets.setdefault(aidx, []).append((s2,s3,s0))
                                faces_aofs.append((s0+abase, s1+abase, s2+abase))
                                faces_aofs.append((s2+abase, s3+abase, s0+abase))
                                a_buckets_aofs.setdefault(aidx, []).append((s0+abase, s1+abase, s2+abase))
                                a_buckets_aofs.setdefault(aidx, []).append((s2+abase, s3+abase, s0+abase))
                            else:
                                faces.append((s3,s0,s1))
                                faces.append((s1,s2,s3))
                                a_buckets.setdefault(aidx, []).append((s3,s0,s1))
                                a_buckets.setdefault(aidx, []).append((s1,s2,s3))
                                faces_aofs.append((s3+abase, s0+abase, s1+abase))
                                faces_aofs.append((s1+abase, s2+abase, s3+abase))
                                a_buckets_aofs.setdefault(aidx, []).append((s3+abase, s0+abase, s1+abase))
                                a_buckets_aofs.setdefault(aidx, []).append((s1+abase, s2+abase, s3+abase))
                        else:
                            faces.append((s3,s0,s1))
                            faces.append((s1,s2,s3))
                            a_buckets.setdefault(aidx, []).append((s3,s0,s1))
                            a_buckets.setdefault(aidx, []).append((s1,s2,s3))
                            faces_aofs.append((s3+abase, s0+abase, s1+abase))
                            faces_aofs.append((s1+abase, s2+abase, s3+abase))
                            a_buckets_aofs.setdefault(aidx, []).append((s3+abase, s0+abase, s1+abase))
                            a_buckets_aofs.setdefault(aidx, []).append((s1+abase, s2+abase, s3+abase))
                # Optional component extraction
                if components and faces:
                    from collections import defaultdict, deque
                    v2f = defaultdict(list)
                    for i,(a,b,c) in enumerate(faces):
                        v2f[a].append(i); v2f[b].append(i); v2f[c].append(i)
                    adj = [set() for _ in faces]
                    for i,(a,b,c) in enumerate(faces):
                        s = set(v2f[a]) | set(v2f[b]) | set(v2f[c]); s.discard(i); adj[i]=s
                    visited=set(); comps=[]
                    for i in range(len(faces)):
                        if i in visited: continue
                        q=deque([i]); comp=[]
                        while q:
                            j=q.popleft()
                            if j in visited: continue
                            visited.add(j); comp.append(j)
                            q.extend(adj[j])
                        comps.append(comp)
                    # sort by size for readability (no filtering here)
                    comps.sort(key=len, reverse=True)
                    # write components as separate groups in same OBJ
                    for ci, comp in enumerate(comps):
                        f.write(f"g comp_{ci}\n")
                        for j in comp:
                            a,b,c = faces[j]
                            if vn_mode == 'off':
                                f.write(f"f {a+1} {b+1} {c+1}\n")
                            else:
                                f.write(f"f {a+1}//{a+1} {b+1}//{b+1} {c+1}//{c+1}\n")
                    faces_written = sum(len(comp) for comp in comps)
                else:
                    faces_written = 0
                    for a,b,c in faces:
                        if vn_mode == 'off':
                            f.write(f"f {a+1} {b+1} {c+1}\n")
                        else:
                            f.write(f"f {a+1}//{a+1} {b+1}//{b+1} {c+1}//{c+1}\n")
                        faces_written += 1
            written.append(outp)
            if verbose:
                print(f"[wrote] {outp} (faces={faces_written}) alt_b={alt_b}")
            # Optional: also emit a base-offset variant (indices offset by A.short0 per D.a_index)
            outp_aofs = os.path.join(dst_dir, f"{base}_ofs{off}_global_aofs.obj")
            with open(outp_aofs, 'w', encoding='utf-8') as fa:
                fa.write(f"# Global mesh with indices offset by owning A C-slice start, {os.path.basename(path)} @0x{off:X}\n")
                fa.write("g global_aofs\n")
                for cr in summary.c_records:
                    fa.write(f"v {cr.f0:.6f} {cr.f1:.6f} {cr.f2:.6f}\n")
                if vn_mode != 'off':
                    if vn_mode == 'b':
                        for cr in summary.c_records:
                            if 0 <= cr.b_index < len(summary.b_records):
                                nx,ny,nz = summary.b_records[cr.b_index]
                                fa.write(f"vn {nx:.6f} {ny:.6f} {nz:.6f}\n")
                            else:
                                fa.write("vn 0 0 1\n")
                    elif vn_mode == 'tri':
                        for _ in summary.c_records:
                            fa.write("vn 0 0 1\n")
                faces_written_aofs = 0
                for a,b,c in faces_aofs:
                    if vn_mode == 'off':
                        fa.write(f"f {a+1} {b+1} {c+1}\n")
                    else:
                        fa.write(f"f {a+1}//{a+1} {b+1}//{b+1} {c+1}//{c+1}\n")
                    faces_written_aofs += 1
            written.append(outp_aofs)
            if verbose:
                print(f"[wrote] {outp_aofs} (faces={faces_written_aofs}, A-offset indices)")
            # Optional grouped-by-A-index global output for hub localization analysis
            if group_by_a and summary.c_records and summary.d_records:
                outp_grp = os.path.join(dst_dir, f"{base}_ofs{off}_global_byA.obj")
                with open(outp_grp, 'w', encoding='utf-8') as fg:
                    fg.write(f"# Global mesh grouped per A-index from {os.path.basename(path)} @0x{off:X}\n")
                    # Emit positions once (same indexing as plain global)
                    for cr in summary.c_records:
                        fg.write(f"v {cr.f0:.6f} {cr.f1:.6f} {cr.f2:.6f}\n")
                    # Optional vertex normals list to keep f//vn references consistent with indices
                    if vn_mode != 'off':
                        if vn_mode == 'b':
                            for cr in summary.c_records:
                                if 0 <= cr.b_index < len(summary.b_records):
                                    nx,ny,nz = summary.b_records[cr.b_index]
                                    fg.write(f"vn {nx:.6f} {ny:.6f} {nz:.6f}\n")
                                else:
                                    fg.write("vn 0 0 1\n")
                        elif vn_mode == 'tri':
                            for _ in summary.c_records:
                                fg.write("vn 0 0 1\n")
                    # Write groups sorted by A-index for readability
                    total_grp_faces = 0
                    for aidx in sorted(a_buckets.keys()):
                        fg.write(f"g a_{aidx:03d}\n")
                        for a,b,c in a_buckets[aidx]:
                            if vn_mode == 'off':
                                fg.write(f"f {a+1} {b+1} {c+1}\n")
                            else:
                                fg.write(f"f {a+1}//{a+1} {b+1}//{b+1} {c+1}//{c+1}\n")
                            total_grp_faces += 1
                written.append(outp_grp)
                if verbose:
                    print(f"[wrote] {outp_grp} (grouped by A; faces={total_grp_faces})")
                # Also emit grouped-by-A with A-offset indices for direct per-group absolute mapping
                outp_grp_aofs = os.path.join(dst_dir, f"{base}_ofs{off}_global_byA_aofs.obj")
                with open(outp_grp_aofs, 'w', encoding='utf-8') as fg2:
                    fg2.write(f"# Global mesh grouped per A-index with A-offset indices, {os.path.basename(path)} @0x{off:X}\n")
                    for cr in summary.c_records:
                        fg2.write(f"v {cr.f0:.6f} {cr.f1:.6f} {cr.f2:.6f}\n")
                    if vn_mode != 'off':
                        if vn_mode == 'b':
                            for cr in summary.c_records:
                                if 0 <= cr.b_index < len(summary.b_records):
                                    nx,ny,nz = summary.b_records[cr.b_index]
                                    fg2.write(f"vn {nx:.6f} {ny:.6f} {nz:.6f}\n")
                                else:
                                    fg2.write("vn 0 0 1\n")
                        elif vn_mode == 'tri':
                            for _ in summary.c_records:
                                fg2.write("vn 0 0 1\n")
                    total_grp_faces_aofs = 0
                    for aidx in sorted(a_buckets_aofs.keys()):
                        fg2.write(f"g a_{aidx:03d}_aofs\n")
                        for a,b,c in a_buckets_aofs[aidx]:
                            if vn_mode == 'off':
                                fg2.write(f"f {a+1} {b+1} {c+1}\n")
                            else:
                                fg2.write(f"f {a+1}//{a+1} {b+1}//{b+1} {c+1}//{c+1}\n")
                            total_grp_faces_aofs += 1
                written.append(outp_grp_aofs)
                if verbose:
                    print(f"[wrote] {outp_grp_aofs} (grouped by A with A-offset indices; faces={total_grp_faces_aofs})")
            # Optional: also emit a global mesh variant remapped through Section B positions
            if dual_sources and summary.b_records:
                outp_b = os.path.join(dst_dir, f"{base}_ofs{off}_global_b.obj")
                with open(outp_b, 'w', encoding='utf-8') as f:
                    f.write(f"# Global mesh with positions substituted from Section B via C->B map, {os.path.basename(path)} @0x{off:X}\n")
                    f.write("g global_b\n")
                    # Emit positions from Section B when mapping valid; fallback to C on out-of-range
                    for cr in summary.c_records:
                        if 0 <= cr.b_index < len(summary.b_records):
                            bx,by,bz = summary.b_records[cr.b_index]
                            f.write(f"v {bx:.6f} {by:.6f} {bz:.6f}\n")
                        else:
                            f.write(f"v {cr.f0:.6f} {cr.f1:.6f} {cr.f2:.6f}\n")
                    # Optional vertex normals (reuse same policy)
                    if vn_mode != 'off':
                        if vn_mode == 'b':
                            for cr in summary.c_records:
                                if 0 <= cr.b_index < len(summary.b_records):
                                    nx,ny,nz = summary.b_records[cr.b_index]
                                    f.write(f"vn {nx:.6f} {ny:.6f} {nz:.6f}\n")
                                else:
                                    f.write("vn 0 0 1\n")
                        elif vn_mode == 'tri':
                            for _ in summary.c_records:
                                f.write("vn 0 0 1\n")
                    # Faces identical to C-domain global (indices reference same order)
                    for a,b,c in faces:
                        if vn_mode == 'off':
                            f.write(f"f {a+1} {b+1} {c+1}\n")
                        else:
                            f.write(f"f {a+1}//{a+1} {b+1}//{b+1} {c+1}//{c+1}\n")
                written.append(outp_b)
                if verbose:
                    print(f"[wrote] {outp_b} (global B-positions variant)")
            # Normal consistency pass (only global mode): recompute face normals and compare with Section B vectors
            if faces_written:
                total = 0
                dot_count = 0
                dot_sum = 0.0
                dot_above_08 = 0
                # Simple normal computation for each tri (no quad special-case here; already split)
                import math
                def get_v(i):
                    if alt_b and 0 <= summary.c_records[i].b_index < len(summary.b_records):
                        return summary.b_records[summary.c_records[i].b_index]
                    cr = summary.c_records[i]
                    return (cr.f0,cr.f1,cr.f2)
                def get_bnorm(i):
                    cr = summary.c_records[i]
                    bi = cr.b_index
                    if 0 <= bi < len(summary.b_records):
                        bx,by,bz = summary.b_records[bi]
                        l = math.sqrt(bx*bx+by*by+bz*bz) or 1.0
                        return (bx/l,by/l,bz/l)
                    return None
                def tri_normal(a,b,c):
                    ax,ay,az=a; bx,by,bz=b; cx,cy,cz=c
                    ux,uy,uz = bx-ax, by-ay, bz-az
                    vx,vy,vz = cx-ax, cy-ay, cz-az
                    nx = uy*vz - uz*vy
                    ny = uz*vx - ux*vz
                    nz = ux*vy - uy*vx
                    l = math.sqrt(nx*nx+ny*ny+nz*nz) or 1.0
                    return (nx/l,ny/l,nz/l)
                # Iterate D faces again
                for d in summary.d_records:
                    s0,s1,s2,s3 = d.indices
                    if s2 == s3:
                        a,b,c = get_v(s0), get_v(s1), get_v(s2)
                        triN = tri_normal(a,b,c)
                        total += 1
                        # Compare with available per-vertex B vectors
                        for vi in (s0,s1,s2):
                            bn = get_bnorm(vi)
                            if bn is None: continue
                            dot = abs(triN[0]*bn[0] + triN[1]*bn[1] + triN[2]*bn[2])
                            dot_sum += dot; dot_count += 1
                            if dot >= 0.8: dot_above_08 += 1
                    else:
                        # two triangles
                        a1,b1,c1 = get_v(s3), get_v(s0), get_v(s1)
                        a2,b2,c2 = get_v(s1), get_v(s2), get_v(s3)
                        triN1 = tri_normal(a1,b1,c1); triN2 = tri_normal(a2,b2,c2)
                        total += 2
                        for triN, (x,y,z) in ((triN1,(s3,s0,s1)), (triN2,(s1,s2,s3))):
                            for vi in (x,y,z):
                                bn = get_bnorm(vi)
                                if bn is None: continue
                                dot = abs(triN[0]*bn[0] + triN[1]*bn[1] + triN[2]*bn[2])
                                dot_sum += dot; dot_count += 1
                                if dot >= 0.8: dot_above_08 += 1
                stats_path = os.path.join(dst_dir, f"{base}_ofs{off}_global_stats.json")
                with open(stats_path,'w',encoding='utf-8') as sf:
                    import json
                    avg_dot = (dot_sum/dot_count) if dot_count else None
                    json.dump({
                        'faces': faces_written,
                        'triangles_processed': total,
                        'bnorm_vertex_samples': dot_count,
                        'bnorm_avg_abs_dot_vs_tri_normal': avg_dot,
                        'bnorm_frac_abs_dot_ge_0.8': (dot_above_08/dot_count) if dot_count else None,
                        'alt_b_positions': alt_b
                    }, sf, indent=2)
                if verbose:
                    print(f"[stats] wrote {stats_path}")
    return written


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description='Export PSM2 per-J-record vertex slices to OBJ (provisional faces)')
    ap.add_argument('--src', default='out/maps', help='directory containing .psm2 files or containers')
    ap.add_argument('--dst', default='out/obj_psm2', help='output directory for OBJ files')
    ap.add_argument('--limit', type=int, default=50, help='max files to process in --src')
    ap.add_argument('--faces', choices=['slice','global','off'], default='slice', help='face emission mode')
    ap.add_argument('--alt-b', action='store_true', help='use Section B remapped vertex positions (global mode only)')
    ap.add_argument('--verbose', action='store_true')
    ap.add_argument('--vn', choices=['off','b','tri'], default='b', help='vertex normals: off, from Section B map (b), or computed (tri)')
    ap.add_argument('--components', action='store_true', help='group faces into connected components (no filtering)')
    ap.add_argument('--dual-sources', action='store_true', help='in slice mode, also emit a Section-B substituted variant per J-record')
    ap.add_argument('--group-by-a', action='store_true', help='in global mode, also emit a variant grouped by DRecord.a_index')
    ap.add_argument('--quad-diag', choices=['fixed','auto'], default='fixed', help='quad split: fixed (3,0,1)+(1,2,3) or auto-pick diagonal by area')
    args = ap.parse_args(argv)
    rx_psm2 = re.compile(r'.*\.psm2$', re.I)
    rx_known = re.compile(r'^map_(\d{4})\.dec\.bin$', re.I)
    names = sorted(os.listdir(args.src))
    files = [os.path.join(args.src, n) for n in names if rx_psm2.match(n) or rx_known.match(n)]
    total_written = 0
    for i, pth in enumerate(files):
        if i >= args.limit:
            break
        outs = export_psm2(pth, args.dst, verbose=args.verbose, faces_mode=args.faces, alt_b=args.alt_b, vn_mode=args.vn,
                           components=args.components, dual_sources=args.dual_sources, group_by_a=args.group_by_a,
                           quad_diag=args.quad_diag)
        total_written += len(outs)
    print(f"Processed {min(args.limit, len(files))} files; wrote {total_written} OBJ(s) to {args.dst}")
    return 0

if __name__ == '__main__':
    import sys
    raise SystemExit(main(sys.argv[1:]))
