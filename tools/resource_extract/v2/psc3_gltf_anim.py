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
    UV_SCALE,
    parse_psc3_full,
    sample_pose_v2,
    _norm_for,
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
    _primary_subdraw_idx,
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

    Empirically verified convention (validated against in-game capture
    of grp_0001 aid2): YXZ with positive signs and the standard Z-up ->
    Y-up axis-component swap.

        q = q_y(ey) * q_x(ex) * q_z(ez)

    Derivation: per-bone runtime build path
    ``FUN_0020cf28`` (param_10==0, called from ``FUN_0020d618``)
    constructs the matrix as
        M = I -> diag(s) -> *R_z(-ez) -> *R_x(-ex) -> *R_y(-ey) -> *T
    where each per-axis cell function (FUN_0020ba30/ba88/bae0) writes
    four entries of the working buffer and a VU0 microprogram at
    ``_vcallms 0x60`` composes it with the running matrix. The PS2
    runtime uses row-vector convention (v' = v * M), so the per-bone
    rotation applied to a row vector is equivalent to a column-vector
    rotation by R_y(ey) * R_x(ex) * R_z(ez) -- hence the YXZ order with
    POSITIVE signs in the column-vector quaternion form glTF uses.

    The Z-up -> Y-up axis component swap (x, y, z) -> (x, z, -y) is
    applied after composition to match the translation swap.

    The entity-root caller (``FUN_0020cdc0``) passes ``param_10 = 1``
    which selects the XYZ branch; that is the world transform of the
    entity, not a per-bone transform, and is irrelevant to PSC3 bone
    poses.
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

    qy = axis(ey, 1)
    qx = axis(ex, 0)
    qz = axis(ez, 2)
    q = qmul(qmul(qy, qx), qz)
    qx_, qy_, qz_, qw_ = q
    # Z-up -> Y-up axis component swap (x, y, z) -> (x, z, -y).
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
        # Each entry says "interpolate to target N over duration D frames".
        # In-game, entry[0]'s duration is the blend-in window from
        # whatever pose the previous animation left, so for an isolated
        # export we treat target[0] as the start pose at t=0 and only
        # advance time for entries 1..N-1.
        times: List[float] = [0.0]
        targets: List[int] = [entries[0].target]
        t = 0.0
        for e in entries[1:]:
            t += e.duration / 60.0
            times.append(t)
            targets.append(e.target)
        return times, targets

    # ---- geometry ----
    # Use the static face builder to get material info & ordering, but
    # we discard its per-submesh grouping: PSC3 uses per-vertex skinning
    # (a primitive's verts can come from multiple bones' vstream
    # windows), so the correct per-vertex bone is determined by which
    # submesh's window contains that vertex, not by which submesh owns
    # the primitive. We rebuild a flat list of (orig_vi, uv, mat_key)
    # corners below so JOINTS_0 can be set per vertex.
    groups, mat_data, mat_order = _build_face_groups(mesh, apply_pose=False)
    active_sm = sorted({sm for (sm, _k) in groups.keys()})

    # Per-vertex bone owner: vert_idx -> sm_idx. Every vertex falls in
    # exactly one submesh's [vstream_start, vstream_end) window.
    n_verts = len(mesh.positions)
    vert_owner: List[int] = [-1] * n_verts
    for sm in mesh.submeshes:
        for v in range(sm.vstream_start, sm.vstream_end):
            if 0 <= v < n_verts:
                vert_owner[v] = sm.index

    def _resolve_uv_for_prim(prim) -> List[Tuple[float, float]]:
        sd_idx = _primary_subdraw_idx(prim)
        if sd_idx is None or not (0 <= sd_idx < len(mesh.subdraws)):
            return [(0.0, 0.0)] * 4
        sd = mesh.subdraws[sd_idx]
        out: List[Tuple[float, float]] = []
        for slot in sd.uvs:
            u = (slot & 0xFF) * UV_SCALE
            v = ((slot >> 8) & 0xFF) * UV_SCALE
            out.append((u, v))
        return out

    # Material key for a primitive (matches _build_face_groups logic).
    def _mat_key_for_prim(prim) -> Optional[Tuple[int, int, int, int]]:
        sd_idx = _primary_subdraw_idx(prim)
        if sd_idx is None or not (0 <= sd_idx < len(mesh.subdraws)):
            return None
        sd = mesh.subdraws[sd_idx]
        flags = sd.tex_flags
        r, g, b = mesh.color_for(sd_idx, 0)
        return (r, g, b, flags)

    # Group triangles by material only. Each tri corner is (orig_vi, uv).
    skinned_tris: Dict[Tuple[int, int, int, int], List[List[Tuple[int, Tuple[float, float]]]]] = {}
    for prim in mesh.primitives:
        if prim.flags & 0x20:
            continue
        mk = _mat_key_for_prim(prim)
        if mk is None or mk not in mat_data:
            continue
        uvs = _resolve_uv_for_prim(prim)
        bucket = skinned_tris.setdefault(mk, [])
        if prim.is_triangle:
            bucket.append([
                (prim.v[0], uvs[0]),
                (prim.v[1], uvs[1]),
                (prim.v[2], uvs[2]),
            ])
        else:
            c0 = (prim.v[0], uvs[0])
            c1 = (prim.v[1], uvs[1])
            c2 = (prim.v[2], uvs[2])
            c3 = (prim.v[3], uvs[3])
            bucket.append([c3, c0, c1])
            bucket.append([c1, c2, c3])

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

    # ---- joint table & per-bone rest matrices ------------------------
    # Every submesh becomes a joint. Order = mesh.submeshes order; this
    # is the order used when emitting JOINTS_0 indices and the order
    # of inverseBindMatrices below.
    joint_sm_indices: List[int] = [sm.index for sm in mesh.submeshes]
    sm_idx_to_joint: Dict[int, int] = {sm.index: j for j, sm in enumerate(mesh.submeshes)}

    # ---- single skinned mesh ------------------------------------------
    # One glTF primitive per material. Each vertex carries its owning
    # bone via JOINTS_0 (single influence, weight 1.0). Vertices remain
    # in bone-local space; inverseBindMatrices are identity (see notes
    # at the skin block below).
    def _u8x4(items: List[Tuple[int, int, int, int]]) -> bytes:
        out = bytearray()
        for a, b, c, d in items:
            out.append(a & 0xff); out.append(b & 0xff)
            out.append(c & 0xff); out.append(d & 0xff)
        return bytes(out)

    primitives_json: List[dict] = []
    for mat_key in mat_order:
        if mat_key not in skinned_tris:
            continue
        tris = skinned_tris[mat_key]
        # De-duplicate corners by (orig_vi, uv) — same vertex with the
        # same UV across triangles can share an emitted vertex.
        corner_to_idx: Dict[Tuple[int, Tuple[float, float]], int] = {}
        positions: List[Tuple[float, float, float]] = []
        normals: List[Tuple[float, float, float]] = []
        uvs: List[Tuple[float, float]] = []
        joints: List[Tuple[int, int, int, int]] = []
        weights: List[Tuple[float, float, float, float]] = []
        indices: List[int] = []
        for tri in tris:
            for (orig_vi, uv) in tri:
                key = (orig_vi, uv)
                idx = corner_to_idx.get(key)
                if idx is None:
                    p = mesh.positions[orig_vi] if 0 <= orig_vi < n_verts else (0.0, 0.0, 0.0)
                    n = _norm_for(mesh, orig_vi)
                    # Z-up -> Y-up (matches bone TRS swap).
                    pos = (p[0], p[2], -p[1])
                    nrm = (n[0], n[2], -n[1])
                    owner_sm = vert_owner[orig_vi] if 0 <= orig_vi < n_verts else -1
                    if owner_sm < 0:
                        # Fall back to the world-root submesh (sm37 / first
                        # entry whose parent_idx == -1 or self) — should not
                        # happen for valid PSC3 data.
                        owner_sm = mesh.submeshes[0].index
                    joint_idx = sm_idx_to_joint[owner_sm]
                    idx = len(positions)
                    corner_to_idx[key] = idx
                    positions.append(pos)
                    normals.append(nrm)
                    uvs.append(uv)
                    joints.append((joint_idx, 0, 0, 0))
                    weights.append((1.0, 0.0, 0.0, 0.0))
                indices.append(idx)
        if not positions:
            continue
        pos_off = binbuf.append(_f32_vec3(positions))
        nrm_off = binbuf.append(_f32_vec3(normals))
        uv_off = binbuf.append(_f32_vec2(uvs))
        # JOINTS_0 as UNSIGNED_BYTE VEC4 (5121).
        j_off = binbuf.append(_u8x4(joints))
        w_off = binbuf.append(_f32_vec4(weights))
        idx_off = binbuf.append(_u16_idx(indices), align=2)
        bv_pos = _add_bv(pos_off, len(positions) * 12, target=34962)
        bv_nrm = _add_bv(nrm_off, len(normals) * 12, target=34962)
        bv_uv = _add_bv(uv_off, len(uvs) * 8, target=34962)
        bv_j = _add_bv(j_off, len(joints) * 4, target=34962)
        bv_w = _add_bv(w_off, len(weights) * 16, target=34962)
        bv_idx = _add_bv(idx_off, len(indices) * 2, target=34963)
        mn, mx = _bbox(positions)
        a_pos = _add_acc(bv_pos, len(positions), "VEC3", 5126, mn, mx)
        a_nrm = _add_acc(bv_nrm, len(normals), "VEC3", 5126)
        a_uv = _add_acc(bv_uv, len(uvs), "VEC2", 5126)
        a_j = _add_acc(bv_j, len(joints), "VEC4", 5121)
        a_w = _add_acc(bv_w, len(weights), "VEC4", 5126)
        a_idx = _add_acc(bv_idx, len(indices), "SCALAR", 5123)
        primitives_json.append({
            "attributes": {
                "POSITION": a_pos,
                "NORMAL": a_nrm,
                "TEXCOORD_0": a_uv,
                "JOINTS_0": a_j,
                "WEIGHTS_0": a_w,
            },
            "indices": a_idx,
            "material": mat_key_to_index[mat_key],
            "mode": 4,
        })

    meshes_json: List[dict] = []
    if primitives_json:
        meshes_json.append({
            "name": f"{name}_skinned",
            "primitives": primitives_json,
        })
    skinned_mesh_index = 0 if meshes_json else None

    # ---- node hierarchy + skin ---------------------------------------
    # Layout:
    #   node 0  : virtual scene root (holds the bone tree + skinned mesh)
    #   nodes 1..N: one per submesh, parented per Submesh.parent_idx.
    #   last    : the skinned mesh node (no TRS, references skin).
    # Bone nodes carry TRS (rest pose) but no `mesh` attribute — geometry
    # lives on the skinned mesh node and is positioned via the skin.
    nodes: List[dict] = [{"name": name, "children": []}]
    sm_to_node_index: Dict[int, int] = {}
    for sm in mesh.submeshes:
        tr_raw, eu_raw, _scale = sample_pose_v2(buf, mesh, sm.index, 0)
        tx, ty, tz = _swap_trans(tr_raw)
        qx, qy, qz, qw = _euler_to_quat(*eu_raw)
        node: dict = {
            "name": f"{name}_sm{sm.index:02d}",
            "translation": [tx, ty, tz],
            "rotation": [qx, qy, qz, qw],
        }
        sm_to_node_index[sm.index] = len(nodes)
        nodes.append(node)
    # Wire parents.
    for sm in mesh.submeshes:
        my_node = sm_to_node_index[sm.index]
        if sm.parent_idx >= 0 and sm.parent_idx in sm_to_node_index:
            parent_node = sm_to_node_index[sm.parent_idx]
        else:
            parent_node = 0
        nodes[parent_node].setdefault("children", []).append(my_node)

    # Joint node indices in the same order as joint_sm_indices.
    joint_node_indices: List[int] = [sm_to_node_index[s] for s in joint_sm_indices]

    skins_json: List[dict] = []
    if skinned_mesh_index is not None:
        # Inverse bind matrices: identity for every joint. Vertex
        # positions in the PSC3 are stored in each owning bone's local
        # space, so at rest:
        #   final = node_world_at_rest * IBM * v_local
        #         = bone_world_at_rest * I * v_local
        #         = bone_world_at_rest * v_local
        # which is the correct rest-pose placement.
        ibm_data = bytearray()
        identity = (1.0, 0.0, 0.0, 0.0,
                    0.0, 1.0, 0.0, 0.0,
                    0.0, 0.0, 1.0, 0.0,
                    0.0, 0.0, 0.0, 1.0)
        for _ in joint_node_indices:
            ibm_data.extend(struct.pack("<16f", *identity))
        ibm_off = binbuf.append(bytes(ibm_data))
        bv_ibm = _add_bv(ibm_off, len(ibm_data))
        a_ibm = _add_acc(bv_ibm, len(joint_node_indices), "MAT4", 5126)
        skins_json.append({
            "name": f"{name}_skin",
            "joints": joint_node_indices,
            "inverseBindMatrices": a_ibm,
            "skeleton": joint_node_indices[0] if joint_node_indices else 0,
        })
        # Skinned mesh node: no TRS, references the skin + mesh. Live
        # under the virtual root so it renders alongside the skeleton.
        skinned_node_idx = len(nodes)
        nodes.append({
            "name": f"{name}_mesh",
            "mesh": skinned_mesh_index,
            "skin": 0,
        })
        nodes[0].setdefault("children", []).append(skinned_node_idx)

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
        for sm in mesh.submeshes:
            sm_idx = sm.index
            if sm_idx not in sm_to_node_index:
                continue
            trans_kf: List[Tuple[float, float, float]] = []
            rot_kf: List[Tuple[float, float, float, float]] = []
            for tgt in targets:
                tr, eu, _scale = sample_pose_v2(buf, mesh, sm_idx, tgt)
                trans_kf.append(_swap_trans(tr))
                q = _euler_to_quat(*eu)
                # Hemisphere-correct successive quats so that LINEAR
                # interpolation in glTF doesn't take the long way around
                # the 4-sphere. q and -q represent the same rotation; we
                # negate q whenever its dot with the previous keyframe is
                # negative.
                if rot_kf:
                    pq = rot_kf[-1]
                    dot = q[0]*pq[0] + q[1]*pq[1] + q[2]*pq[2] + q[3]*pq[3]
                    if dot < 0.0:
                        q = (-q[0], -q[1], -q[2], -q[3])
                rot_kf.append(q)
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
    if skins_json:
        gltf["skins"] = skins_json
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
    ap.add_argument('--flat-aids', action='store_true',
                    help="Use legacy sibling layout <dst>_aid<N>/ "
                         "instead of nested <dst>/aid<N>/.")
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

    # Default: one .gltf per anim id, nested under --dst as
    # <dst>/aid<N>/<name>.gltf so a single mesh's animations group
    # together in one folder. Pass --flat-aids to fall back to the
    # legacy sibling layout (<dst>_aid<N>/).
    parent = args.dst.rstrip('/\\')
    for aid in anim_ids:
        if args.flat_aids:
            out_dir = f"{parent}_aid{aid}"
        else:
            out_dir = os.path.join(parent, f"aid{aid}")
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
