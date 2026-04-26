#!/usr/bin/env python3
"""PSC3 -> glTF 2.0 emitter.

Reuses ``psc3_full.parse_psc3_full`` to get the mesh, then writes a
hand-rolled glTF 2.0 (separate .gltf JSON manifest + .bin buffer).

No external dependencies. PNG textures are referenced by relative URI
(siblings of the chosen --src file, matching mtl behavior).

Per-primitive layout in the glTF output:
  * One glTF mesh per PSC3 record.
  * One glTF primitive per (submesh, material) tuple — same grouping used
    by the OBJ writer.
  * Per primitive: a fresh, fully-expanded vertex stream (every triangle
    corner gets its own POSITION/NORMAL/TEXCOORD_0). Indices are
    sequential u16. This wastes bytes vs. dedup, but is trivially correct
    against the PS2 fixed-point UVs/colors.

Coordinate convention: matches OBJ output (Z-up -> Y-up: x, z, -y).
UV V is flipped (PS2 -> glTF).

Materials:
  * Textured (slot != 0x7f and enable bit set): ``baseColorTexture``
    pointing at the bundle's preferred BMPA PNG; ``baseColorFactor``
    white.
  * Untextured: ``baseColorFactor`` set to the PSC3 vertex color (corner
    0 of the subdraw), no texture.

Run:
    python -m tools.resource_extract.v2.psc3_gltf \\
        --src out/target/s14_e030/grp_0157.psc3 \\
        --dst out/target/extract_gltf
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from typing import Dict, List, Optional, Tuple

from .psc3_full import (
    MAGIC_PSC3,
    PSC3FullMesh,
    Primitive,
    Submesh,
    UV_SCALE,
    _norm_for,
    _pose_normal,
    _pose_vertex,
    _u32,
    parse_psc3_full,
)


# ---------------------------------------------------------------------------
# Buffer accumulator
# ---------------------------------------------------------------------------

class _BinBuf:
    """Accumulates binary chunks and tracks per-chunk byte offsets."""

    def __init__(self) -> None:
        self.chunks: List[bytes] = []
        self.length: int = 0

    def append(self, data: bytes, align: int = 4) -> int:
        """Pad to ``align`` then append; returns the byte offset of ``data``."""
        pad = (-self.length) % align
        if pad:
            self.chunks.append(b"\x00" * pad)
            self.length += pad
        off = self.length
        self.chunks.append(data)
        self.length += len(data)
        return off

    def bytes(self) -> bytes:
        # glTF requires the buffer length be a multiple of 4.
        pad = (-self.length) % 4
        if pad:
            self.chunks.append(b"\x00" * pad)
            self.length += pad
        return b"".join(self.chunks)


def _f32_vec3(seq: List[Tuple[float, float, float]]) -> bytes:
    out = bytearray()
    for x, y, z in seq:
        out += struct.pack('<fff', x, y, z)
    return bytes(out)


def _f32_vec2(seq: List[Tuple[float, float]]) -> bytes:
    out = bytearray()
    for u, v in seq:
        out += struct.pack('<ff', u, v)
    return bytes(out)


def _u16_idx(seq: List[int]) -> bytes:
    out = bytearray()
    for i in seq:
        out += struct.pack('<H', i)
    return bytes(out)


def _bbox(seq: List[Tuple[float, float, float]]) -> Tuple[List[float], List[float]]:
    if not seq:
        return ([0.0, 0.0, 0.0], [0.0, 0.0, 0.0])
    mn = [seq[0][0], seq[0][1], seq[0][2]]
    mx = [seq[0][0], seq[0][1], seq[0][2]]
    for x, y, z in seq[1:]:
        if x < mn[0]: mn[0] = x
        if y < mn[1]: mn[1] = y
        if z < mn[2]: mn[2] = z
        if x > mx[0]: mx[0] = x
        if y > mx[1]: mx[1] = y
        if z > mx[2]: mx[2] = z
    return (mn, mx)


# ---------------------------------------------------------------------------
# Mesh -> face descriptors  (mirrors psc3_full.write_obj_mtl pass 1)
# ---------------------------------------------------------------------------

def _primary_subdraw_idx(p: Primitive) -> Optional[int]:
    if p.primary_subdraw < 4:
        sd = p.subdraws[p.primary_subdraw]
        if sd != -1:
            return sd
    for sd in p.subdraws:
        if sd != -1:
            return sd
    return None


def _build_face_groups(mesh: PSC3FullMesh, apply_pose: bool):
    """Group triangles by (submesh, material_key).

    Returns:
        groups: dict[(sm_idx, mat_key)] -> list of triangles, where each
                triangle is a list of 3 corners. A corner is
                (pos:vec3, normal:vec3, uv:vec2).
        mat_key_data: dict[mat_key] -> dict(name, kd, flags, sample_subdraw)
        mat_order: ordered list of mat_keys (insertion order)
    """
    groups: Dict[Tuple[int, Tuple[int, int, int, int]], List] = {}
    mat_data: Dict[Tuple[int, int, int, int], dict] = {}
    mat_order: List[Tuple[int, int, int, int]] = []

    def _resolve_uv(subdraw_idx: int) -> List[Tuple[float, float]]:
        if 0 <= subdraw_idx < len(mesh.subdraws):
            sd = mesh.subdraws[subdraw_idx]
            uvs = []
            for slot in sd.uvs:
                # UV byte order: low byte = U, high byte = V. No V flip.
                u = (slot & 0xFF) * UV_SCALE
                v = ((slot >> 8) & 0xFF) * UV_SCALE
                uvs.append((u, v))
            return uvs
        return [(0.0, 0.0)] * 4

    def _mat_key(subdraw_idx: int) -> Tuple[int, int, int, int]:
        sd = mesh.subdraws[subdraw_idx] if 0 <= subdraw_idx < len(mesh.subdraws) else None
        flags = sd.tex_flags if sd else 0
        r, g, b = mesh.color_for(subdraw_idx, 0)
        key = (r, g, b, flags)
        if key not in mat_data:
            mat_data[key] = {
                'name': f"mat_{len(mat_order):04d}",
                'kd': (r / 255.0, g / 255.0, b / 255.0),
                'flags': flags,
                'sample_subdraw': subdraw_idx,
            }
            mat_order.append(key)
        return key

    def _vertex_for(orig_vi: int, sm: Submesh, uv: Tuple[float, float]):
        p = mesh.positions[orig_vi] if 0 <= orig_vi < len(mesh.positions) else (0.0, 0.0, 0.0)
        n = _norm_for(mesh, orig_vi)
        if apply_pose:
            p = _pose_vertex(sm, p)
            n = _pose_normal(sm, n)
        # Z-up -> Y-up: (x, y, z) -> (x, z, -y)
        return ((p[0], p[2], -p[1]), (n[0], n[2], -n[1]), uv)

    for prim in mesh.primitives:
        if prim.flags & 0x20:
            continue
        sd_idx = _primary_subdraw_idx(prim)
        if sd_idx is None:
            continue
        sm = mesh.primitive_submesh(prim.index)
        if sm is None:
            continue
        key = _mat_key(sd_idx)
        gkey = (sm.index, key)
        bucket = groups.setdefault(gkey, [])
        uvs = _resolve_uv(sd_idx)

        if prim.is_triangle:
            tri = [
                _vertex_for(prim.v[0], sm, uvs[0]),
                _vertex_for(prim.v[1], sm, uvs[1]),
                _vertex_for(prim.v[2], sm, uvs[2]),
            ]
            bucket.append(tri)
        else:
            # Quad split: matches OBJ writer's split (v3,v0,v1)+(v1,v2,v3).
            c0 = _vertex_for(prim.v[0], sm, uvs[0])
            c1 = _vertex_for(prim.v[1], sm, uvs[1])
            c2 = _vertex_for(prim.v[2], sm, uvs[2])
            c3 = _vertex_for(prim.v[3], sm, uvs[3])
            bucket.append([c3, c0, c1])
            bucket.append([c1, c2, c3])

    return groups, mat_data, mat_order


# ---------------------------------------------------------------------------
# glTF assembly
# ---------------------------------------------------------------------------

def _bundle_pngs(bundle_dir: str) -> List[str]:
    if not bundle_dir or not os.path.isdir(bundle_dir):
        return []
    try:
        return sorted(fn for fn in os.listdir(bundle_dir)
                      if fn.startswith('tex_') and fn.endswith('.png'))
    except OSError:
        return []


# Cache for grp_tex_map.json lookups so we don't re-read per file.
_GRP_TEX_MAP_CACHE: dict = {}


def _load_grp_tex_map(bundle_dir: Optional[str]) -> dict:
    """Look up <bundle_dir>/grp_tex_map.json (or repo-root out/grp_tex_map.json
    as a fallback) and return the parsed mapping. Returns {} if not found.
    Result is cached per directory."""
    if not bundle_dir:
        return {}
    bundle_dir = os.path.abspath(bundle_dir)
    if bundle_dir in _GRP_TEX_MAP_CACHE:
        return _GRP_TEX_MAP_CACHE[bundle_dir]

    candidates = [os.path.join(bundle_dir, 'grp_tex_map.json')]
    # Walk upwards looking for an out/grp_tex_map.json (repo-root convention).
    cur = bundle_dir
    for _ in range(6):
        cur = os.path.dirname(cur)
        if not cur or cur == os.path.dirname(cur):
            break
        candidates.append(os.path.join(cur, 'out', 'grp_tex_map.json'))

    data: dict = {}
    for path in candidates:
        if os.path.isfile(path):
            try:
                import json
                with open(path, 'r') as f:
                    data = json.load(f)
                break
            except (OSError, ValueError):
                continue
    _GRP_TEX_MAP_CACHE[bundle_dir] = data
    return data


def _authoritative_png(name: str, pngs: List[str],
                       bundle_dir: Optional[str]) -> Optional[str]:
    """Return the PNG mandated by either the SLUS-derived
    ``grp_tex_map.json`` or by bundle-adjacency layout in the unpack
    ``_manifest.txt``. Returns None when neither source agrees.

    When this returns a value, callers should treat it as a hard
    override of the per-material 'untextured' flag bits, since the
    flag decode is not 100% reliable.
    """
    if not name.startswith(('grp_', 'map_')):
        return None
    if name.startswith('grp_'):
        rid = name[len('grp_'):]
        mp = _load_grp_tex_map(bundle_dir)
        tex_map = mp.get('grp_to_tex') if isinstance(mp, dict) else None
        if isinstance(tex_map, dict):
            tid = tex_map.get(rid.lower()) or tex_map.get(rid)
            if isinstance(tid, str):
                candidate = f"tex_{tid}.png"
                if candidate in pngs:
                    return candidate
    return _adjacency_png(name, pngs, bundle_dir)


# Cache for parsed _manifest.txt files. Maps bundle_dir -> ordered list of
# {"cat": "grp"|"map"|"tex"|..., "rid": "0001", "name": "grp_0001.psc3"}.
_BUNDLE_MANIFEST_CACHE: dict = {}


def _load_bundle_manifest(bundle_dir: Optional[str]):
    """Parse `<bundle_dir>/_manifest.txt` (written by mcb_unpack_all) and
    return an ordered list of records, or None if not available.

    Each record is a dict ``{cat, rid, base}`` -- ``base`` matches the
    file basename without extension (e.g. ``grp_013c``).
    """
    if not bundle_dir:
        return None
    bd = os.path.abspath(bundle_dir)
    if bd in _BUNDLE_MANIFEST_CACHE:
        return _BUNDLE_MANIFEST_CACHE[bd]
    path = os.path.join(bd, '_manifest.txt')
    out = None
    if os.path.isfile(path):
        try:
            recs = []
            with open(path, 'r', encoding='utf-8') as f:
                for line in f:
                    line = line.strip()
                    if not line.startswith('@'):
                        continue
                    parts = line.split()
                    # @offset id cat rid raw_size kind written
                    if len(parts) < 7:
                        continue
                    cat, rid_hex = parts[2], parts[3]
                    if not rid_hex.startswith('0x'):
                        continue
                    rid = int(rid_hex, 16)
                    base = f"{cat}_{rid:04x}"
                    recs.append({'cat': cat, 'rid': rid, 'base': base})
            out = recs
        except OSError:
            out = None
    _BUNDLE_MANIFEST_CACHE[bd] = out
    return out


def _adjacency_png(name: str, pngs: List[str],
                   bundle_dir: Optional[str]) -> Optional[str]:
    """Bundle-adjacency texture lookup.

    Within an MCB bundle, each PSC3 mesh (or contiguous block of PSC3
    meshes) is immediately followed in record order by the texture
    BMPA that belongs to it. We use this layout as the primary
    resolver for textures, since it matches how the runtime streams
    each scene's per-character/per-prop texture pages alongside the
    meshes.

    For a query record ``name`` (e.g. ``grp_013d``), we walk forward
    through the manifest from that record until we hit the next
    ``tex`` record and return its PNG name -- so a block of
    ``grp_013d``/``grp_013e``/``grp_013f`` followed by ``tex_0189``
    all map to ``tex_0189.png``.
    """
    recs = _load_bundle_manifest(bundle_dir)
    if not recs:
        return None
    target_base = name.lower()
    idx = None
    for i, r in enumerate(recs):
        if r['base'] == target_base:
            idx = i
            break
    if idx is None:
        return None
    # Walk forward to next tex record.
    for r in recs[idx + 1:]:
        if r['cat'] == 'tex':
            cand = f"tex_{r['rid']:04x}.png"
            if cand in pngs:
                return cand
            return None  # tex was found in manifest but missing on disk
    return None


def _adjacency_pngs(name: str, pngs: List[str],
                    bundle_dir: Optional[str]) -> List[str]:
    """Return ALL contiguous ``tex`` records that follow ``name`` in the
    bundle manifest, in manifest order.

    PSM2 maps consume multiple texture pages (one per material slot in
    section [E] byte 6); the runtime streams them as a contiguous block
    of BMPAs immediately after the PSM2 record, in the same order the
    section-E byte-6 indices reference (0..N-1). Callers index into this
    list by the per-corner texture-page byte to bind the right PNG.
    """
    recs = _load_bundle_manifest(bundle_dir)
    if not recs:
        return []
    target_base = name.lower()
    idx = None
    for i, r in enumerate(recs):
        if r['base'] == target_base:
            idx = i
            break
    if idx is None:
        return []
    out: List[str] = []
    for r in recs[idx + 1:]:
        if r['cat'] != 'tex':
            break
        cand = f"tex_{r['rid']:04x}.png"
        if cand in pngs:
            out.append(cand)
        else:
            out.append("")  # placeholder to preserve indexing
    return out


def _preferred_png(name: str, pngs: List[str],
                   bundle_dir: Optional[str] = None) -> Optional[str]:
    """Pick the best PNG for a grp_<rid>.psc3 name.

    Resolution order:
      1. grp_tex_map.json -- authoritative mapping baked into
         SLUS_200.11 (the entity-type -> mesh/tex descriptor tables).
         Limited to the ~635 entities the engine references by id from
         the static descriptor tables.
      2. Bundle-adjacency lookup via the unpack manifest. Each PSC3
         (or consecutive block of PSC3s) is followed in the bundle by
         its texture BMPA; this resolves anything the SLUS map misses,
         including map_* records and rid ranges (like 0x013c..0x013f
         in s01_e013) that aren't in the descriptor tables.
      3. Same-id filename match (tex_<rid>.png).
      4. First available PNG (legacy fallback, only when no manifest).
    """
    if name.startswith(('grp_', 'map_')):
        rid = name.split('_', 1)[1]
        # 1. authoritative SLUS map (grp only)
        if name.startswith('grp_'):
            mp = _load_grp_tex_map(bundle_dir)
            tex_map = mp.get('grp_to_tex') if isinstance(mp, dict) else None
            if isinstance(tex_map, dict):
                tid = tex_map.get(rid.lower()) or tex_map.get(rid)
                if isinstance(tid, str):
                    candidate = f"tex_{tid}.png"
                    if candidate in pngs:
                        return candidate
        # 2. bundle adjacency
        adj = _adjacency_png(name, pngs, bundle_dir)
        if adj:
            return adj
        # 3. same-id filename match
        match = f"tex_{rid}.png"
        if match in pngs:
            return match
    # 4. anything goes
    return pngs[0] if pngs else None


def write_gltf(mesh: PSC3FullMesh, gltf_path: str, name: str,
               apply_rest_pose: bool = False,
               bundle_dir: Optional[str] = None,
               png_override: Optional[str] = None) -> dict:
    bin_path = os.path.splitext(gltf_path)[0] + ".bin"
    bin_uri = os.path.basename(bin_path)

    groups, mat_data, mat_order = _build_face_groups(mesh, apply_rest_pose)

    pngs = _bundle_pngs(bundle_dir) if bundle_dir else []
    preferred = png_override if png_override else _preferred_png(name, pngs, bundle_dir)
    force_tex = bool(png_override) or bool(_authoritative_png(name, pngs, bundle_dir))

    # ---- material -> glTF material index ------------------------------
    # Textured materials all share the preferred PNG (a probe-and-swap
    # workflow per the OBJ MTL output). One image+texture entry total.
    images: List[dict] = []
    textures: List[dict] = []
    samplers: List[dict] = [
        {"magFilter": 9729, "minFilter": 9729, "wrapS": 10497, "wrapT": 10497}  # LINEAR + REPEAT
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
        # An authoritative texture binding (--png or grp_tex_map.json hit)
        # overrides the per-material flag decode, which is not always
        # reliable (see grp_0183).
        if force_tex:
            no_tex = False
        m: dict = {"name": d['name']}
        pbr: dict = {"metallicFactor": 0.0, "roughnessFactor": 1.0}
        if no_tex or base_tex_index is None:
            kd = d['kd']
            pbr["baseColorFactor"] = [kd[0], kd[1], kd[2], 1.0]
        else:
            pbr["baseColorTexture"] = {"index": base_tex_index, "texCoord": 0}
            pbr["baseColorFactor"] = [1.0, 1.0, 1.0, 1.0]
        m["pbrMetallicRoughness"] = pbr
        m["doubleSided"] = True
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

    # ---- accumulate buffers / accessors / primitives ------------------
    binbuf = _BinBuf()
    buffer_views: List[dict] = []
    accessors: List[dict] = []
    primitives_json: List[dict] = []

    def _add_bufview(byte_offset: int, byte_length: int, target: Optional[int] = None) -> int:
        bv: dict = {"buffer": 0, "byteOffset": byte_offset, "byteLength": byte_length}
        if target is not None:
            bv["target"] = target
        buffer_views.append(bv)
        return len(buffer_views) - 1

    def _add_accessor(bv: int, count: int, ctype: str, comp_type: int,
                      mn=None, mx=None) -> int:
        a: dict = {
            "bufferView": bv, "byteOffset": 0,
            "componentType": comp_type, "count": count, "type": ctype,
        }
        if mn is not None: a["min"] = mn
        if mx is not None: a["max"] = mx
        accessors.append(a)
        return len(accessors) - 1

    # Stable iteration order (sm_idx, then mat_key insertion order).
    for (sm_idx, mat_key) in sorted(groups.keys(),
                                    key=lambda k: (k[0], mat_order.index(k[1]))):
        tris = groups[(sm_idx, mat_key)]
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

        bv_pos = _add_bufview(pos_off, len(positions) * 12, target=34962)  # ARRAY_BUFFER
        bv_nrm = _add_bufview(nrm_off, len(normals) * 12, target=34962)
        bv_uv = _add_bufview(uv_off, len(uvs) * 8, target=34962)
        bv_idx = _add_bufview(idx_off, len(indices) * 2, target=34963)  # ELEMENT_ARRAY_BUFFER

        mn, mx = _bbox(positions)
        a_pos = _add_accessor(bv_pos, len(positions), "VEC3", 5126, mn, mx)  # FLOAT
        a_nrm = _add_accessor(bv_nrm, len(normals), "VEC3", 5126)
        a_uv = _add_accessor(bv_uv, len(uvs), "VEC2", 5126)
        a_idx = _add_accessor(bv_idx, len(indices), "SCALAR", 5123)  # UNSIGNED_SHORT

        primitives_json.append({
            "attributes": {"POSITION": a_pos, "NORMAL": a_nrm, "TEXCOORD_0": a_uv},
            "indices": a_idx,
            "material": mat_key_to_index[mat_key],
            "mode": 4,  # TRIANGLES
            "extras": {"submesh": sm_idx},
        })

    bin_blob = binbuf.bytes()

    # If we ended up with no primitives at all, emit an empty scene
    # rather than an invalid mesh entry (glTF requires primitives.length >= 1).
    if not primitives_json:
        gltf: dict = {
            "asset": {"version": "2.0", "generator": "psc3_gltf.py"},
            "scene": 0,
            "scenes": [{"nodes": []}],
        }
        with open(gltf_path, 'w', encoding='utf-8') as fg:
            json.dump(gltf, fg, indent=2)
        # Don't write a 0-byte buffer.
        return {
            'submeshes': len(mesh.submeshes), 'primitives': 0,
            'materials': 0, 'bin_bytes': 0,
            'gltf_path': gltf_path, 'bin_path': None,
            'preferred_png': preferred,
        }

    with open(bin_path, 'wb') as fb:
        fb.write(bin_blob)

    gltf: dict = {
        "asset": {"version": "2.0", "generator": "psc3_gltf.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"name": name, "primitives": primitives_json}],
        "buffers": [{"uri": bin_uri, "byteLength": len(bin_blob)}],
        "bufferViews": buffer_views,
        "accessors": accessors,
        "materials": materials,
    }
    if textures:
        gltf["textures"] = textures
    if images:
        gltf["images"] = images
    if samplers and textures:
        gltf["samplers"] = samplers

    with open(gltf_path, 'w', encoding='utf-8') as fg:
        json.dump(gltf, fg, indent=2)

    return {
        'submeshes': len(mesh.submeshes),
        'primitives': len(primitives_json),
        'materials': len(materials),
        'bin_bytes': len(bin_blob),
        'gltf_path': gltf_path,
        'bin_path': bin_path,
        'preferred_png': preferred,
    }


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def export_file(src_path: str, dst_dir: str, verbose: bool = False,
                apply_pose: bool = False,
                png_override: Optional[str] = None,
                pose_frame: int = 0) -> Optional[dict]:
    data = open(src_path, 'rb').read()
    if len(data) < 4 or _u32(data, 0) != MAGIC_PSC3:
        if verbose:
            print(f"[skip] {src_path}: not PSC3")
        return None
    os.makedirs(dst_dir, exist_ok=True)
    base = os.path.splitext(os.path.basename(src_path))[0]
    try:
        mesh = parse_psc3_full(data, pose_frame=pose_frame)
    except Exception as e:
        if verbose:
            print(f"[err]  {src_path}: {e}")
        return None
    gltf_path = os.path.join(dst_dir, base + ".gltf")
    bundle_dir = os.path.dirname(os.path.abspath(src_path))

    # Copy referenced PNG into the dst dir so the .gltf URI resolves
    # without needing the user to ship the bundle dir alongside.
    pngs = _bundle_pngs(bundle_dir)
    preferred = png_override if png_override else _preferred_png(base, pngs, bundle_dir)
    if preferred:
        src_png = os.path.join(bundle_dir, preferred)
        dst_png = os.path.join(dst_dir, preferred)
        if os.path.abspath(src_png) != os.path.abspath(dst_png):
            try:
                with open(src_png, 'rb') as fi, open(dst_png, 'wb') as fo:
                    fo.write(fi.read())
            except OSError:
                pass

    stats = write_gltf(mesh, gltf_path, name=base,
                       apply_rest_pose=apply_pose,
                       bundle_dir=bundle_dir,
                       png_override=png_override)
    if verbose:
        print(f"[ok]   {gltf_path}  prims={stats['primitives']}  "
              f"mats={stats['materials']}  bin={stats['bin_bytes']}B  "
              f"png={stats['preferred_png']}")
    return stats


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="PSC3 -> glTF 2.0 emitter")
    ap.add_argument('--src', required=True, help="PSC3 file or directory")
    ap.add_argument('--dst', required=True, help="Output directory")
    ap.add_argument('--limit', type=int, default=None)
    ap.add_argument('-v', '--verbose', action='store_true')
    ap.add_argument('--pose', action='store_true',
                    help="Apply per-submesh pose-table transform.")
    ap.add_argument('--pose-frame', type=int, default=0,
                    help="Pose-table index to bake into vertices when "
                         "--pose is set. Default 0 (rest pose). Each "
                         "submesh has up to N entries; out-of-range "
                         "falls back to identity.")
    ap.add_argument('--scan-poses', action='store_true',
                    help="Print per-submesh pose-table sizes for the "
                         "input(s) and exit (no glTF output).")
    ap.add_argument('--png', default=None,
                    help="Override the auto-picked BMPA PNG basename "
                         "(e.g. --png tex_0178.png). Must exist in the "
                         "same directory as --src.")
    args = ap.parse_args(argv)

    inputs: List[str] = []
    if os.path.isdir(args.src):
        for fn in sorted(os.listdir(args.src)):
            full = os.path.join(args.src, fn)
            if os.path.isfile(full) and fn.lower().endswith(('.psc3', '.bin')):
                inputs.append(full)
    else:
        inputs.append(args.src)
    if args.limit is not None:
        inputs = inputs[:args.limit]

    if args.scan_poses:
        for p in inputs:
            data = open(p, 'rb').read()
            if len(data) < 4 or _u32(data, 0) != MAGIC_PSC3:
                continue
            try:
                mesh = parse_psc3_full(data)
            except Exception:
                continue
            print(f"\n{os.path.basename(p)}")
            for sm in mesh.submeshes:
                n = sm.byte_len // 4 if sm.byte_len > 0 else 0
                print(f"  sm{sm.index:2d}: poses={n}  slab@{sm.section_a_off:#06x}")
        return 0

    ok = 0
    for p in inputs:
        if export_file(p, args.dst, verbose=args.verbose, apply_pose=args.pose,
                       png_override=args.png, pose_frame=args.pose_frame):
            ok += 1
    print(f"Processed {len(inputs)} file(s); {ok} extracted to {args.dst}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
