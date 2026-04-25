#!/usr/bin/env python3
"""PSC3 -> glTF 2.0 with per-submesh animation tracks.

Companion to ``psc3_gltf.py`` (static-mesh emitter). Reuses its
geometry pipeline (``_build_face_groups``) but splits the result into
one glTF node per active submesh and emits glTF Animation tracks
driven by the in-PSC3 timeline data.

Coordinate convention: vertices come from ``_build_face_groups``
already swapped to Y-up via (x, y, z) -> (x, z, -y). Pose translations
and quaternions sampled from the PSC3 are in Z-up space, so we apply
the same swap before emitting them as glTF node TRS values:

  trans:  (tx, ty, tz) -> (tx, tz, -ty)
  quat:   (qx, qy, qz, qw) -> (qx, qz, -qy, qw)

CLI:
    python -m tools.resource_extract.v2.psc3_gltf_anim \
        --src out/target/s00_e000/grp_0183.psc3 \
        --dst out/anim/grp_0183 \
        --anim-id 1
"""
from __future__ import annotations

import argparse
import json
import math
import os
import struct
import sys
from typing import Dict, List, Optional, Tuple

from .psc3_full import (
    MAGIC_PSC3,
    PSC3FullMesh,
    parse_psc3_full,
    sample_pose_v2,
    _u32,
)
from .psc3_gltf import (
    _BinBuf,
    _bbox,
    _bundle_pngs,
    _f32_vec2,
    _f32_vec3,
    _preferred_png,
    _authoritative_png,
    _u16_idx,
    _build_face_groups,
)
from .psc3_anim_decode import parse_anim_table, parse_timeline


def _f32_vec4(items: List[Tuple[float, float, float, float]]) -> bytes:
    out = bytearray()
    for x, y, z, w in items:
        out.extend(struct.pack("<ffff", x, y, z, w))
    return bytes(out)


def _f32_scalar(items: List[float]) -> bytes:
    return b"".join(struct.pack("<f", v) for v in items)


def _swap_trans(t):
    """PS2 Z-up -> glTF Y-up: (x, y, z) -> (x, z, -y)."""
    x, y, z = t
    return (x, z, -y)


def _euler_to_quat(ex: float, ey: float, ez: float) -> Tuple[float, float, float, float]:
    """Convert PS2 euler triple to a unit quaternion in glTF Y-up space.

    The runtime matrix builder (FUN_0020cf28, param_10==0 branch) does:
        M = T * R_y(-ey) * R_x(-ex) * R_z(-ez) * S
    so the equivalent quaternion is q = q_y(-ey) * q_x(-ex) * q_z(-ez).
    Z-up -> Y-up basis swap (x,y,z) -> (x,z,-y) is applied to the axis
    components after composition.
    """
    def axis(theta: float, ax: int) -> Tuple[float, float, float, float]:
        s = math.sin(theta * 0.5)
        c = math.cos(theta * 0.5)
        v = [0.0, 0.0, 0.0]
        v[ax] = s
        return (v[0], v[1], v[2], c)

    def qmul(a, b):
        ax, ay, az, aw = a
        bx, by, bz, bw = b
        return (
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz,
        )

    qy = axis(-ey, 1)
    qx = axis(-ex, 0)
    qz = axis(-ez, 2)
    q = qmul(qmul(qy, qx), qz)
    qx_, qy_, qz_, qw_ = q
    # Basis swap on the axis component to match the position swap.
    return (qx_, qz_, -qy_, qw_)


def emit_animated(buf: bytes, mesh: PSC3FullMesh, gltf_path: str, name: str,
                  anim_ids: Optional[List[int]] = None,
                  bundle_dir: Optional[str] = None,
                  png_override: Optional[str] = None,
                  bind_anim_id: int = 0) -> dict:
    """Emit a single glTF containing one or more animations.

    The mesh, nodes, and materials are shared across all animations;
    each PSC3 anim record becomes a separate glTF Animation entry.
    ``bind_anim_id`` selects which record's target=0 pose is used as
    the static node TRS (defaults to anim 0).
    """
    bin_path = os.path.splitext(gltf_path)[0] + ".bin"
    bin_uri = os.path.basename(bin_path)

    # ---- timeline records --------------------------------------------
    h = mesh.header
    anim_table_off = h['offs_u0c']
    if not anim_table_off:
        raise ValueError("PSC3 has no anim table (offs_u0c == 0)")
    recs = parse_anim_table(buf, anim_table_off)
    if not recs:
        raise ValueError("PSC3 anim table is empty")
    if anim_ids is None:
        anim_ids = list(range(len(recs)))
    for aid in anim_ids:
        if aid < 0 or aid >= len(recs):
            raise ValueError(f"anim_id {aid} out of range; got {len(recs)} records")

    def _timeline_for(aid: int) -> Tuple[List[float], List[int]]:
        rec = recs[aid]
        if aid + 1 < len(recs):
            end_off = recs[aid + 1].timeline_off
        elif 0 < rec.lod_param < 0x200:
            end_off = rec.timeline_off + rec.lod_param
        else:
            end_off = anim_table_off
        entries = parse_timeline(buf, rec.timeline_off, end_off)
        if not entries:
            return [], []
        # Each entry says "interpolate to target N over duration D".
        # Animations start from the bind/rest pose (target=0) at t=0.
        times: List[float] = [0.0]
        targets: List[int] = [0]
        t = 0.0
        for e in entries:
            t += e.duration / 60.0
            times.append(t)
            targets.append(e.target)
        return times, targets

    # ---- geometry ----
    groups, mat_data, mat_order = _build_face_groups(mesh, apply_pose=False)
    sm_groups: Dict[int, Dict[Tuple[int, int, int, int], list]] = {}
    for (sm_idx, mat_key), tris in groups.items():
        sm_groups.setdefault(sm_idx, {})[mat_key] = tris
    active_sm = sorted(sm_groups.keys())

    # ---- texture / material ------------------------------------------
    pngs = _bundle_pngs(bundle_dir) if bundle_dir else []
    preferred = png_override if png_override else _preferred_png(name, pngs, bundle_dir)
    # An authoritative match (from grp_tex_map.json) or an explicit --png
    # override means we trust the texture binding over the per-material
    # 'untextured' flag bits.
    force_tex = bool(png_override) or bool(_authoritative_png(name, pngs, bundle_dir))

    images: List[dict] = []
    textures: List[dict] = []
    samplers_json: List[dict] = [
        {"magFilter": 9729, "minFilter": 9729, "wrapS": 10497, "wrapT": 10497}
    ]
    base_tex_index: Optional[int] = None
    if preferred:
        images.append({"uri": preferred})
        textures.append({"sampler": 0, "source": 0})
        base_tex_index = 0

    materials: List[dict] = []
    mat_key_to_index: Dict[Tuple[int, int, int, int], int] = {}
    for key in mat_order:
        d = mat_data[key]
        flags = d['flags']
        slot = flags & 0x7f
        enable = (flags >> 14) & 0x3
        no_tex = (slot == 0x7f) or (enable == 0)
        # Authoritative texture match (--png or grp_tex_map.json hit) overrides
        # the per-material flag decode, which is not always reliable.
        if force_tex:
            no_tex = False
        m: dict = {"name": d['name'], "doubleSided": True}
        pbr: dict = {"metallicFactor": 0.0, "roughnessFactor": 1.0}
        if no_tex or base_tex_index is None:
            kd = d['kd']
            pbr["baseColorFactor"] = [kd[0], kd[1], kd[2], 1.0]
        else:
            pbr["baseColorTexture"] = {"index": base_tex_index, "texCoord": 0}
            pbr["baseColorFactor"] = [1.0, 1.0, 1.0, 1.0]
        m["pbrMetallicRoughness"] = pbr
        m["extras"] = {
            "tex_flags": f"0x{flags:04x}",
            "slot": slot,
            "palette": (flags >> 7) & 0xf,
            "fmt": (flags >> 11) & 0x7,
            "enable": enable,
            "untextured": bool(no_tex),
        }
        mat_key_to_index[key] = len(materials)
        materials.append(m)

    binbuf = _BinBuf()
    buffer_views: List[dict] = []
    accessors: List[dict] = []

    def _add_bv(off, n, target=None):
        bv: dict = {"buffer": 0, "byteOffset": off, "byteLength": n}
        if target is not None:
            bv["target"] = target
        buffer_views.append(bv)
        return len(buffer_views) - 1

    def _add_acc(bv, count, ctype, comp_type, mn=None, mx=None):
        a: dict = {
            "bufferView": bv, "byteOffset": 0,
            "componentType": comp_type, "count": count, "type": ctype,
        }
        if mn is not None: a["min"] = mn
        if mx is not None: a["max"] = mx
        accessors.append(a)
        return len(accessors) - 1

    meshes_json: List[dict] = []
    sm_to_mesh_index: Dict[int, int] = {}

    for sm_idx in active_sm:
        groups_for_sm = sm_groups[sm_idx]
        primitives_json: List[dict] = []
        for mat_key in sorted(groups_for_sm.keys(), key=lambda k: mat_order.index(k)):
            tris = groups_for_sm[mat_key]
            positions: List[Tuple[float, float, float]] = []
            normals: List[Tuple[float, float, float]] = []
            uvs: List[Tuple[float, float]] = []
            indices: List[int] = []
            for tri in tris:
                for (p, n, uv) in tri:
                    indices.append(len(positions))
                    positions.append(p)
                    normals.append(n)
                    uvs.append(uv)
            if not positions:
                continue
            pos_off = binbuf.append(_f32_vec3(positions))
            nrm_off = binbuf.append(_f32_vec3(normals))
            uv_off = binbuf.append(_f32_vec2(uvs))
            idx_off = binbuf.append(_u16_idx(indices), align=2)
            bv_pos = _add_bv(pos_off, len(positions) * 12, target=34962)
            bv_nrm = _add_bv(nrm_off, len(normals) * 12, target=34962)
            bv_uv = _add_bv(uv_off, len(uvs) * 8, target=34962)
            bv_idx = _add_bv(idx_off, len(indices) * 2, target=34963)
            mn, mx = _bbox(positions)
            a_pos = _add_acc(bv_pos, len(positions), "VEC3", 5126, mn, mx)
            a_nrm = _add_acc(bv_nrm, len(normals), "VEC3", 5126)
            a_uv = _add_acc(bv_uv, len(uvs), "VEC2", 5126)
            a_idx = _add_acc(bv_idx, len(indices), "SCALAR", 5123)
            primitives_json.append({
                "attributes": {"POSITION": a_pos, "NORMAL": a_nrm, "TEXCOORD_0": a_uv},
                "indices": a_idx,
                "material": mat_key_to_index[mat_key],
                "mode": 4,
            })
        if not primitives_json:
            continue
        sm_to_mesh_index[sm_idx] = len(meshes_json)
        meshes_json.append({
            "name": f"{name}_sm{sm_idx:02d}",
            "primitives": primitives_json,
        })

    nodes: List[dict] = [{"name": name, "children": []}]
    sm_to_node_index: Dict[int, int] = {}
    # Bind pose: per-submesh translation + rotation sampled at the
    # bind target (we use target=0 of bind_anim_id; identity for
    # unbound submeshes).
    for sm_idx in active_sm:
        if sm_idx not in sm_to_mesh_index:
            continue
        tr_raw, eu_raw, _scale = sample_pose_v2(buf, mesh, sm_idx, 0)
        tx, ty, tz = _swap_trans(tr_raw)
        qx, qy, qz, qw = _euler_to_quat(*eu_raw)
        node = {
            "name": f"{name}_sm{sm_idx:02d}",
            "mesh": sm_to_mesh_index[sm_idx],
            "translation": [tx, ty, tz],
            "rotation": [qx, qy, qz, qw],
        }
        sm_to_node_index[sm_idx] = len(nodes)
        nodes[0]["children"].append(len(nodes))
        nodes.append(node)

    # ---- animations ---------------------------------------------------
    animations_json: List[dict] = []
    anim_summaries: List[dict] = []
    for aid in anim_ids:
        times, targets = _timeline_for(aid)
        if not times:
            continue
        anim_samplers: List[dict] = []
        anim_channels: List[dict] = []
        time_off = binbuf.append(_f32_scalar(times))
        bv_time = _add_bv(time_off, len(times) * 4)
        a_time = _add_acc(bv_time, len(times), "SCALAR", 5126,
                          mn=[min(times)], mx=[max(times)])
        for sm_idx in active_sm:
            if sm_idx not in sm_to_node_index:
                continue
            trans_kf: List[Tuple[float, float, float]] = []
            rot_kf: List[Tuple[float, float, float, float]] = []
            for tgt in targets:
                tr, eu, _scale = sample_pose_v2(buf, mesh, sm_idx, tgt)
                trans_kf.append(_swap_trans(tr))
                rot_kf.append(_euler_to_quat(*eu))
            t_off = binbuf.append(_f32_vec3(trans_kf))
            r_off = binbuf.append(_f32_vec4(rot_kf))
            bv_t = _add_bv(t_off, len(trans_kf) * 12)
            bv_r = _add_bv(r_off, len(rot_kf) * 16)
            a_t = _add_acc(bv_t, len(trans_kf), "VEC3", 5126)
            a_r = _add_acc(bv_r, len(rot_kf), "VEC4", 5126)
            s_t = len(anim_samplers)
            anim_samplers.append({"input": a_time, "output": a_t, "interpolation": "LINEAR"})
            s_r = len(anim_samplers)
            anim_samplers.append({"input": a_time, "output": a_r, "interpolation": "LINEAR"})
            node_idx = sm_to_node_index[sm_idx]
            anim_channels.append({"sampler": s_t, "target": {"node": node_idx, "path": "translation"}})
            anim_channels.append({"sampler": s_r, "target": {"node": node_idx, "path": "rotation"}})
        animations_json.append({
            "name": f"anim_id_{aid}",
            "samplers": anim_samplers,
            "channels": anim_channels,
        })
        anim_summaries.append({
            'anim_id': aid,
            'channels': len(anim_channels),
            'keyframes': len(times),
            'duration_s': times[-1],
        })

    bin_blob = binbuf.bytes()
    gltf: dict = {
        "asset": {"version": "2.0", "generator": "psc3_gltf_anim.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": nodes,
        "meshes": meshes_json,
        "buffers": [{"uri": bin_uri, "byteLength": len(bin_blob)}],
        "bufferViews": buffer_views,
        "accessors": accessors,
        "materials": materials,
        "animations": animations_json,
    }
    if textures:
        gltf["textures"] = textures
    if images:
        gltf["images"] = images
    if textures:
        gltf["samplers"] = samplers_json
    os.makedirs(os.path.dirname(gltf_path) or ".", exist_ok=True)
    with open(bin_path, 'wb') as fb:
        fb.write(bin_blob)
    with open(gltf_path, 'w', encoding='utf-8') as fg:
        json.dump(gltf, fg, indent=2)
    # Copy the chosen PNG sibling next to the .gltf so it resolves.
    if preferred and bundle_dir:
        src_png = os.path.join(bundle_dir, preferred)
        dst_png = os.path.join(os.path.dirname(gltf_path) or ".", preferred)
        if os.path.abspath(src_png) != os.path.abspath(dst_png):
            try:
                with open(src_png, 'rb') as fi, open(dst_png, 'wb') as fo:
                    fo.write(fi.read())
            except OSError:
                pass
    return {
        'submeshes': len(active_sm),
        'meshes': len(meshes_json),
        'nodes': len(nodes),
        'animations': anim_summaries,
        'bin_bytes': len(bin_blob),
        'gltf_path': gltf_path,
        'bin_path': bin_path,
        'preferred_png': preferred,
        'materials': len(materials),
    }


def _parse_anim_ids(spec: str, total: int) -> List[int]:
    """Parse 'all', '0,2,3', or '0-3' into an ordered list of ids."""
    spec = spec.strip().lower()
    if not spec or spec == 'all':
        return list(range(total))
    out: List[int] = []
    for part in spec.split(','):
        part = part.strip()
        if not part:
            continue
        if '-' in part:
            a, b = part.split('-', 1)
            out.extend(range(int(a), int(b) + 1))
        else:
            out.append(int(part))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', required=True)
    ap.add_argument('--dst', required=True,
                    help="Output base directory. With --multi-anim, all "
                         "anim records share a single .gltf in --dst. "
                         "Otherwise each anim is written to "
                         "<dst>_aid<N>/<name>.gltf.")
    ap.add_argument('--anim-id', default='all',
                    help="Animation id(s) to emit. 'all' (default), a "
                         "comma list ('0,2,3'), a range ('0-3'), or a "
                         "single int.")
    ap.add_argument('--multi-anim', action='store_true',
                    help="Pack all selected anim ids into one .gltf "
                         "(Blender shows them as separate Actions).")
    ap.add_argument('--png', default=None,
                    help="Override the auto-picked BMPA PNG basename "
                         "(e.g. --png tex_0178.png). Must exist next to --src.")
    args = ap.parse_args()
    data = open(args.src, 'rb').read()
    if len(data) < 4 or _u32(data, 0) != MAGIC_PSC3:
        print(f"Not a PSC3: {args.src}", file=sys.stderr)
        return 1
    mesh = parse_psc3_full(data)
    h = mesh.header
    if not h['offs_u0c']:
        print("PSC3 has no animation table", file=sys.stderr)
        return 1
    total = len(parse_anim_table(data, h['offs_u0c']))
    anim_ids = _parse_anim_ids(args.anim_id, total)
    name = os.path.splitext(os.path.basename(args.src))[0]
    bundle_dir = os.path.dirname(os.path.abspath(args.src))

    if args.multi_anim:
        out_gltf = os.path.join(args.dst, f"{name}.gltf")
        stats = emit_animated(data, mesh, out_gltf, name, anim_ids=anim_ids,
                              bundle_dir=bundle_dir, png_override=args.png)
        print(f"Wrote {stats['gltf_path']}")
        print(f"  submeshes={stats['submeshes']}  meshes={stats['meshes']}  "
              f"nodes={stats['nodes']}  materials={stats['materials']}")
        print(f"  bin={stats['bin_bytes']}B  png={stats['preferred_png']}")
        for a in stats['animations']:
            print(f"  anim_id={a['anim_id']}  channels={a['channels']}  "
                  f"keyframes={a['keyframes']}  duration={a['duration_s']:.2f}s")
        return 0

    # Default: one .gltf per anim id, in sibling _aid<N> subdirs.
    parent = args.dst.rstrip('/\\')
    for aid in anim_ids:
        out_dir = f"{parent}_aid{aid}"
        out_gltf = os.path.join(out_dir, f"{name}.gltf")
        stats = emit_animated(data, mesh, out_gltf, name, anim_ids=[aid],
                              bundle_dir=bundle_dir, png_override=args.png)
        print(f"Wrote {stats['gltf_path']}")
        a = stats['animations'][0] if stats['animations'] else None
        info = (f"channels={a['channels']} keyframes={a['keyframes']} "
                f"duration={a['duration_s']:.2f}s") if a else "(no keyframes)"
        print(f"  anim_id={aid}  {info}  png={stats['preferred_png']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
