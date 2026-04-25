#!/usr/bin/env python3
"""Rest-pose experiment harness.

Regenerates multiple OBJ variants of a single PSC3 file using different
pose-interpretation strategies so we can A/B them quickly in Blender.

Usage:
  python tools/resource_extract/v2/psc3_pose_experiments.py \
      --src out/all/mcb_full/s01_e011/grp_0001.psc3 \
      --dst out/psc3_pose_experiments

Each strategy emits ``<base>_<strategy>.obj`` (+ .mtl). Open all of them
in Blender to compare.

Strategies
----------
  raw           : no pose; each submesh untransformed (baseline).
  decomp        : FUN_0020da68 literal scales (xyz/2048, w/4096).
  uniform4096   : all 4 quat components / 4096 (legacy guess).
  recon_w_2048  : xyz/2048, ignore stored w, reconstruct via unit quat.
  recon_w_4096  : xyz/4096, ignore stored w, reconstruct via unit quat.
  trans_only    : translation only, identity rotation.
  tr_half       : xyz/4096 + w/4096, translations halved (1/4096 instead of 1/2048).
  tr_double     : xyz/2048 + w/4096, translations doubled (1/1024).

For any strategy that produces per-submesh duplicate vertices, faces
whose corners straddle different submeshes will naturally have each
corner posed by its owning submesh's transform (vertex duplication
active).
"""
from __future__ import annotations
import argparse
import math
import os
import struct
import sys
from typing import Callable, List, Optional, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import psc3_full as pf  # noqa: E402


def _read_pose_raw(buf: bytes, mesh: pf.PSC3FullMesh) -> List[dict]:
    """Return raw Section B records per submesh: {'q': (x,y,z,w s16s), 't': (x,y,z s16s), 'q_sentinel': bool}."""
    records = []
    # Re-read the header to find sections again.
    h = {
        'offs_section_a': pf._u32(buf, 0x08),
        'offs_section_b': pf._u32(buf, 0x2c),
    }
    secB = h['offs_section_b']
    for sm in mesh.submeshes:
        rec = {'q': None, 't': None, 'q_sentinel': False}
        if sm.section_a_off == 0 or sm.section_a_off + 4 > len(buf):
            records.append(rec)
            continue
        u32 = pf._u32(buf, sm.section_a_off)
        quat_idx = u32 & 0xFFFF
        trans_idx = (u32 >> 16) & 0xFFFF
        if quat_idx != 0xFFFF and secB:
            qbase = secB + quat_idx * 2
            if qbase + 8 <= len(buf):
                q = struct.unpack_from('<4h', buf, qbase)
                rec['q'] = q
                rec['q_sentinel'] = (q[0] == 0x7fff)
        if trans_idx != 0xFFFF and secB:
            tbase = secB + trans_idx * 2
            if tbase + 6 <= len(buf):
                rec['t'] = struct.unpack_from('<3h', buf, tbase)
        records.append(rec)
    return records


def _pose_from_components(qx: float, qy: float, qz: float, qw: float,
                          tx: float, ty: float, tz: float,
                          p: Tuple[float, float, float]) -> Tuple[float, float, float]:
    vx, vy, vz = p
    txc = 2.0 * (qy * vz - qz * vy)
    tyc = 2.0 * (qz * vx - qx * vz)
    tzc = 2.0 * (qx * vy - qy * vx)
    rx = vx + qw * txc + (qy * tzc - qz * tyc) + tx
    ry = vy + qw * tyc + (qz * txc - qx * tzc) + ty
    rz = vz + qw * tzc + (qx * tyc - qy * txc) + tz
    return (rx, ry, rz)


Strategy = Callable[[dict], Tuple[Tuple[float, float, float, float],
                                  Tuple[float, float, float]]]
# Each strategy takes a raw pose record and returns (quat(xyzw), trans).
# Returning (None, None) means "no transform".


def _strat_raw(rec):
    return None, None


def _strat_decomp(rec):
    if rec['q_sentinel'] or rec['q'] is None:
        return None, None
    q = rec['q']
    t = rec['t'] or (0, 0, 0)
    return (q[0] / 2048.0, q[1] / 2048.0, q[2] / 2048.0, q[3] / 4096.0), \
           (t[0] / 2048.0, t[1] / 2048.0, t[2] / 2048.0)


def _strat_uniform4096(rec):
    if rec['q_sentinel'] or rec['q'] is None:
        return None, None
    q = rec['q']
    t = rec['t'] or (0, 0, 0)
    return (q[0] / 4096.0, q[1] / 4096.0, q[2] / 4096.0, q[3] / 4096.0), \
           (t[0] / 2048.0, t[1] / 2048.0, t[2] / 2048.0)


def _reconstruct_w(x: float, y: float, z: float) -> float:
    s = x * x + y * y + z * z
    if s >= 1.0:
        # Non-physical; normalize xyz and zero w.
        inv = 1.0 / math.sqrt(s)
        return 0.0  # caller should normalize
    return math.sqrt(1.0 - s)


def _normalize(q):
    n = math.sqrt(sum(c * c for c in q))
    if n == 0:
        return (0.0, 0.0, 0.0, 1.0)
    return tuple(c / n for c in q)


def _strat_recon_w_2048(rec):
    if rec['q_sentinel'] or rec['q'] is None:
        return None, None
    q = rec['q']
    x, y, z = q[0] / 2048.0, q[1] / 2048.0, q[2] / 2048.0
    w = _reconstruct_w(x, y, z)
    quat = _normalize((x, y, z, w if w > 0 else 1.0))
    t = rec['t'] or (0, 0, 0)
    return quat, (t[0] / 2048.0, t[1] / 2048.0, t[2] / 2048.0)


def _strat_recon_w_4096(rec):
    if rec['q_sentinel'] or rec['q'] is None:
        return None, None
    q = rec['q']
    x, y, z = q[0] / 4096.0, q[1] / 4096.0, q[2] / 4096.0
    w = _reconstruct_w(x, y, z)
    quat = _normalize((x, y, z, w if w > 0 else 1.0))
    t = rec['t'] or (0, 0, 0)
    return quat, (t[0] / 2048.0, t[1] / 2048.0, t[2] / 2048.0)


def _strat_trans_only(rec):
    if rec['q_sentinel']:
        return None, None
    t = rec['t'] or (0, 0, 0)
    return (0.0, 0.0, 0.0, 1.0), (t[0] / 2048.0, t[1] / 2048.0, t[2] / 2048.0)


def _strat_tr_half(rec):
    if rec['q_sentinel'] or rec['q'] is None:
        return None, None
    q = rec['q']
    t = rec['t'] or (0, 0, 0)
    return (q[0] / 4096.0, q[1] / 4096.0, q[2] / 4096.0, q[3] / 4096.0), \
           (t[0] / 4096.0, t[1] / 4096.0, t[2] / 4096.0)


def _strat_tr_double(rec):
    if rec['q_sentinel'] or rec['q'] is None:
        return None, None
    q = rec['q']
    t = rec['t'] or (0, 0, 0)
    return (q[0] / 2048.0, q[1] / 2048.0, q[2] / 2048.0, q[3] / 4096.0), \
           (t[0] / 1024.0, t[1] / 1024.0, t[2] / 1024.0)


STRATEGIES = {
    'raw': _strat_raw,
    'decomp': _strat_decomp,
    'uniform4096': _strat_uniform4096,
    'recon_w_2048': _strat_recon_w_2048,
    'recon_w_4096': _strat_recon_w_4096,
    'trans_only': _strat_trans_only,
    'tr_half': _strat_tr_half,
    'tr_double': _strat_tr_double,
}


def _emit(mesh: pf.PSC3FullMesh, raw: List[dict], strategy: Strategy,
          obj_path: str, name: str) -> dict:
    """Build per-(orig_vi, sm) vertex table, apply strategy's pose, write OBJ+MTL."""
    # Resolve per-submesh transform via strategy.
    sm_xf: List[Optional[Tuple[Tuple[float, float, float, float],
                               Tuple[float, float, float]]]] = []
    for rec in raw:
        quat, trans = strategy(rec)
        if quat is None:
            sm_xf.append(None)
        else:
            sm_xf.append((quat, trans))

    # Build vertex table via primitives (matches psc3_full logic).
    vert_table: List[Tuple[int, int]] = []
    vert_map: dict = {}

    def remap(orig_vi, sm_idx):
        key = (orig_vi, sm_idx)
        idx = vert_map.get(key)
        if idx is None:
            idx = len(vert_table)
            vert_table.append(key)
            vert_map[key] = idx
        return idx

    faces: List[Tuple[int, List[int], List[Tuple[float, float]]]] = []
    vt_entries: List[Tuple[float, float]] = []

    def alloc_vt(uv):
        vt_entries.append(uv)
        return len(vt_entries)

    for p in mesh.primitives:
        if p.flags & 0x20:
            continue
        sm = mesh.primitive_submesh(p.index)
        if sm is None:
            continue
        # UVs
        sd_mat = None
        if p.primary_subdraw < 4:
            sd = p.subdraws[p.primary_subdraw]
            if sd != -1:
                sd_mat = sd
        if sd_mat is None:
            for sd in p.subdraws:
                if sd != -1:
                    sd_mat = sd
                    break
        if sd_mat is None:
            continue
        sd = mesh.subdraws[sd_mat] if 0 <= sd_mat < len(mesh.subdraws) else None
        uvs = []
        if sd is not None:
            for slot in sd.uvs:
                u = ((slot >> 8) & 0xFF) / 255.0
                v = 1.0 - ((slot & 0xFF) / 255.0)
                uvs.append((u, v))
        else:
            uvs = [(0.0, 0.0)] * 4

        if p.is_triangle:
            a, b, c = p.v[0], p.v[1], p.v[2]
            ra = remap(a, sm.index); rb = remap(b, sm.index); rc = remap(c, sm.index)
            ua = alloc_vt(uvs[0]); ub = alloc_vt(uvs[1]); uc = alloc_vt(uvs[2])
            faces.append((sm.index, [ra, rb, rc], [ua, ub, uc]))
        else:
            rs = [remap(p.v[i], sm.index) for i in range(4)]
            us = [alloc_vt(uvs[i]) for i in range(4)]
            faces.append((sm.index, [rs[3], rs[0], rs[1]], [us[3], us[0], us[1]]))
            faces.append((sm.index, [rs[1], rs[2], rs[3]], [us[1], us[2], us[3]]))

    # Emit
    with open(obj_path, 'w', encoding='utf-8') as f:
        f.write(f"# PSC3 pose experiment: {name}\n")
        f.write(f"# verts_emitted={len(vert_table)} faces={len(faces)}\n")
        f.write(f"o {name}\n")
        # Vertices
        minv = [1e9] * 3
        maxv = [-1e9] * 3
        edge_accum = []
        posed_cache: List[Tuple[float, float, float]] = []
        for orig_vi, sm_idx in vert_table:
            p = mesh.positions[orig_vi]
            xf = sm_xf[sm_idx]
            if xf is not None:
                q, t = xf
                p = _pose_from_components(q[0], q[1], q[2], q[3], t[0], t[1], t[2], p)
            posed_cache.append(p)
            x, y, z = p
            for k, v in enumerate(p):
                if v < minv[k]:
                    minv[k] = v
                if v > maxv[k]:
                    maxv[k] = v
            f.write(f"v {x:.6f} {z:.6f} {-y:.6f}\n")
        # UVs
        for uv in vt_entries:
            f.write(f"vt {uv[0]:.6f} {uv[1]:.6f}\n")
        # Faces (no normals for speed)
        for sm_idx, vs, vts in faces:
            # Edge lengths for stats
            for i in range(len(vs)):
                a = posed_cache[vs[i]]
                b = posed_cache[vs[(i + 1) % len(vs)]]
                edge_accum.append(
                    math.sqrt(sum((a[k] - b[k]) ** 2 for k in range(3)))
                )
            parts = [f"{vs[i] + 1}/{vts[i]}" for i in range(len(vs))]
            f.write(f"f {' '.join(parts)}\n")

    edge_accum.sort()
    stats = {
        'strategy': name,
        'nverts_emitted': len(vert_table),
        'nfaces': len(faces),
        'bbox_min': tuple(minv),
        'bbox_max': tuple(maxv),
        'edge_med': edge_accum[len(edge_accum) // 2] if edge_accum else 0.0,
        'edge_p99': edge_accum[int(len(edge_accum) * 0.99)] if edge_accum else 0.0,
        'edge_max': edge_accum[-1] if edge_accum else 0.0,
    }
    return stats


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', default='out/all/mcb_full/s01_e011/grp_0001.psc3')
    ap.add_argument('--dst', default='out/psc3_pose_experiments')
    ap.add_argument('--only', nargs='+',
                    help="Restrict to specific strategies (default: all).")
    args = ap.parse_args(argv)

    os.makedirs(args.dst, exist_ok=True)
    buf = open(args.src, 'rb').read()
    mesh = pf.parse_psc3_full(buf)
    raw = _read_pose_raw(buf, mesh)
    base = os.path.splitext(os.path.basename(args.src))[0]

    strategies = STRATEGIES
    if args.only:
        strategies = {k: v for k, v in STRATEGIES.items() if k in args.only}

    rows = []
    for strat_name, strat in strategies.items():
        obj_path = os.path.join(args.dst, f"{base}_{strat_name}.obj")
        stats = _emit(mesh, raw, strat, obj_path, f"{base}_{strat_name}")
        stats['path'] = obj_path
        rows.append(stats)

    print(f"{'strategy':<14}  {'verts':>6}  {'faces':>6}  "
          f"{'bbox X':>18}  {'bbox Y':>18}  {'bbox Z':>18}  "
          f"{'edge99':>8}  {'edgeMx':>8}")
    for r in rows:
        bx = f"[{r['bbox_min'][0]:+.2f},{r['bbox_max'][0]:+.2f}]"
        by = f"[{r['bbox_min'][1]:+.2f},{r['bbox_max'][1]:+.2f}]"
        bz = f"[{r['bbox_min'][2]:+.2f},{r['bbox_max'][2]:+.2f}]"
        print(f"{r['strategy']:<14}  {r['nverts_emitted']:>6}  "
              f"{r['nfaces']:>6}  {bx:>18}  {by:>18}  {bz:>18}  "
              f"{r['edge_p99']:>8.3f}  {r['edge_max']:>8.3f}")
    print(f"\nOBJs written to {args.dst}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
