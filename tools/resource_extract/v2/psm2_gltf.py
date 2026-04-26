#!/usr/bin/env python3
"""PSM2 -> glTF 2.0 emitter.

Parses a PSM2 map mesh via :mod:`psm2.parse_psm2` and writes a glTF
manifest + .bin buffer (mirrors :mod:`psc3_gltf` style; no external deps).

Texture binding uses the same bundle-adjacency resolver as
:mod:`psc3_gltf` -- each PSM2 (or contiguous block of PSM2/PSC3 records)
is followed in the bundle's _manifest.txt by its BMPA texture record,
so we walk forward to the next ``tex`` entry and bind that PNG.

Per-corner UV indices come from PSM2's section D u16[6..9] (each is an
index into the 12-byte section-E attribute table). Sentinel value
0xFFFF means "no corner record" -- the runtime renders such corners as
solid gray (0x404040). For glTF output we don't have a clean way to
mix per-corner solid color and texture inside one primitive, so we fall
back to UV (0, 0) for sentinel corners (the rendered triangle will pull
the top-left texel of the page; usually unobtrusive on map geometry).

UV decoding (per the section-E 12-byte record format):
  Each PSM2 face has 4 corner UV-table indices in section D's u16[6..9].
  Each corner index points to a *separate* 12-byte section-E record (not
  4 corners packed into one record -- the runtime's FUN_0022c3d8
  iterates all 4 corners and copies all 12 bytes of each corner's E
  record verbatim into the corner slot at +0x30+i*0xC).

  Per-corner E record (12 bytes, single corner):
    byte   0     : U pixel (0..255 over the 256-wide texture page)
    byte   1     : V pixel (0..255 over the 256-tall texture page)
    bytes  2..5  : ST/Q multipliers used at runtime to compute the
                   GS-side perspective-corrected (s, t, q) values via
                       s = U/256 * Q
                       t = V/256 * Q
                   (verified against PCSX2 GS dump: ST/Q effective
                   pixel coordinates fall on integer multiples of 1.0,
                   matching b[0]/b[1].)
    byte   6     : texture-page index into the bundle's adjacency PNGs.
    byte   7     : alpha/blend mode (low cardinality).
    byte   8     : per-corner tag (0x0F sentinel -> remap 0x09).
    byte   9     : flag.
    byte   10    : per-corner alpha (halved by FUN_0022c3d8).
    byte   11    : flags (0x70 mask checked).

  glTF rendering doesn't apply the GS Q multiplier (it's only relevant
  for screen-space perspective correction inside the rasterizer); we
  emit (b[0]/256, b[1]/256) directly as UV. The runtime's Q makes the
  in-game render perspective-correct in screen space but the texel
  sampling location at each vertex is always (b[0]/256, b[1]/256).

V is flipped to match glTF's (0,0)=top-left convention, same as
psc3_gltf.py.

Coordinates: PSM2 is Z-up world space; we emit (x, z, -y) for Y-up
glTF viewers, identical to psm2.py's OBJ writer.
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from typing import Dict, List, Optional, Tuple

from .psm2 import MAGIC_PSM2, PSM2Mesh, parse_psm2, _u32
from .psc3_gltf import (_bundle_pngs, _preferred_png, _authoritative_png,
                        _adjacency_pngs)


# UV scale: each component is an unsigned byte where 0..255 maps to
# normalized texture coords [0..1) over the 256x256 PSM2 texture page.
UV_DIVISOR = 256.0


# ---------------------------------------------------------------------------
# Buffer accumulator (same as psc3_gltf._BinBuf)
# ---------------------------------------------------------------------------

class _BinBuf:
    def __init__(self) -> None:
        self.chunks: List[bytes] = []
        self.length: int = 0

    def append(self, data: bytes, align: int = 4) -> int:
        pad = (-self.length) % align
        if pad:
            self.chunks.append(b"\x00" * pad)
            self.length += pad
        off = self.length
        self.chunks.append(data)
        self.length += len(data)
        return off

    def bytes(self) -> bytes:
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


def _u32_idx(seq: List[int]) -> bytes:
    out = bytearray()
    for i in seq:
        out += struct.pack('<I', i)
    return bytes(out)


def _bbox(seq: List[Tuple[float, float, float]]):
    if not seq:
        return ([0.0, 0.0, 0.0], [0.0, 0.0, 0.0])
    mn = list(seq[0])
    mx = list(seq[0])
    for x, y, z in seq[1:]:
        if x < mn[0]: mn[0] = x
        if y < mn[1]: mn[1] = y
        if z < mn[2]: mn[2] = z
        if x > mx[0]: mx[0] = x
        if y > mx[1]: mx[1] = y
        if z > mx[2]: mx[2] = z
    return (mn, mx)


# ---------------------------------------------------------------------------
# UV decoding
# ---------------------------------------------------------------------------

def _decode_corner_uv(record: bytes, corner: int) -> Tuple[float, float]:
    """Decode one of the four corner UVs packed into a 12-byte E record.

    Verified layout (after correcting parser offset bug — E payload starts
    at section_base + 2, not + 4):

      byte[0,1]   -> corner 0 (D0) (U_pixel, V_pixel) on 256x256 page
      byte[2,3]   -> corner 1 (D1)
      byte[4,5]   -> corner 2 (D2)
      byte[6,7]   -> corner 3 (D3)
      byte[8]     -> texture page index (0..7) into bundle adjacency PNGs
      byte[9]     -> alpha mode
      byte[10,11] -> flags

    Verified deterministically against 6 ground-truth UVs across two
    adjacent prims sharing an edge in map_0002 (door panel):

      prim[3186] indices=(4524,4523,4522,4521) e_idx=2643
      bytes=[63,129, 46,170, 46,204, 63,255, 2,0, 0,0]
        D0(-1,0)    -> (63,129) display (0.246, 0.496)  ~user (0.233, 0.479)
        D2(-0.7,-0.9)-> (46,204) display (0.180, 0.203)  ~user (0.174, 0.186)
        D3(-1,-1.5) -> (63,255) display (0.246, 0.004)  ~user (0.232, 0.006)

      prim[3193] indices=(4517,4521,4522,4520) e_idx=2650
      bytes=[1,255, 63,255, 46,204, 18,204, 2,0, 0,0]
        D0(0,-1.5)  -> ( 1,255) display (0.004, 0.004)  ~user (0.017, 0.021)
        D1(-1,-1.5) -> (63,255) display (0.246, 0.004)  ~user (0.234, 0.021)
        D2(-0.7,-0.9)-> (46,204) display (0.180, 0.203)  ~user (0.180, 0.203)
    """
    if len(record) < 8:
        return (0.0, 0.0)
    c = corner & 3
    u_off = c * 2
    v_off = c * 2 + 1
    return (record[u_off] / UV_DIVISOR, record[v_off] / UV_DIVISOR)


def _corner_uv(mesh: PSM2Mesh, prim_idx: int, corner: int) -> Tuple[float, float]:
    """Resolve a primitive corner's (U, V) from the prim's single E record.

    All four `prim_uv_indices` slots hold the same E index. The corner
    parameter selects which of the four (u, v) byte pairs inside that
    12-byte record to read.
    """
    if prim_idx < 0 or prim_idx >= len(mesh.prim_uv_indices):
        return (0.0, 0.0)
    e_idx = mesh.prim_uv_indices[prim_idx][0]
    if e_idx >= 0x8000 or e_idx >= len(mesh.uv_records):
        return (0.0, 0.0)
    return _decode_corner_uv(mesh.uv_records[e_idx], corner)


def _corner_tex_page(mesh: PSM2Mesh, uv_idx: int) -> Optional[int]:
    """Return byte[8] of the section-E record for ``uv_idx``, or None for
    sentinel/out-of-range indices.

    byte[8] is the per-corner texture-page index into the bundle's
    adjacency PNG list (verified against the loader's record layout:
    the first 8 bytes are 4 corner UV pairs, byte[8]=page, byte[9]=alpha,
    byte[10..11]=flags).
    """
    # Any signed-negative index is a sentinel (solid color from the K
    # table or 0xFFFF gray); they don't carry a texture page.
    if uv_idx >= 0x8000 or uv_idx >= len(mesh.uv_records):
        return None
    rec = mesh.uv_records[uv_idx]
    if len(rec) < 9:
        return None
    return rec[8]


def _prim_tex_page(mesh: PSM2Mesh, prim_idx: int) -> int:
    """Pick a primitive's texture-page index by majority-vote of its
    four corner E records (0xFFFF/out-of-range corners ignored; if all
    are invalid, return 0).
    """
    counts: Dict[int, int] = {}
    if prim_idx < len(mesh.prim_uv_indices):
        for c in mesh.prim_uv_indices[prim_idx]:
            page = _corner_tex_page(mesh, c)
            if page is None:
                continue
            counts[page] = counts.get(page, 0) + 1
    if not counts:
        return 0
    # Return the most-frequent (ties broken by lowest page id for
    # determinism).
    return min(counts.items(), key=lambda kv: (-kv[1], kv[0]))[0]


# ---------------------------------------------------------------------------
# Triangle expansion
# ---------------------------------------------------------------------------

def _build_corners(mesh: PSM2Mesh) -> Tuple[
        List[Tuple[float, float, float]],   # positions
        List[Tuple[float, float, float]],   # normals
        List[Tuple[float, float]],          # uvs
        Dict[int, List[int]],               # page -> indices (sequential)
]:
    positions: List[Tuple[float, float, float]] = []
    normals: List[Tuple[float, float, float]] = []
    uvs: List[Tuple[float, float]] = []
    groups: Dict[int, List[int]] = {}

    n_pos = len(mesh.positions)
    n_nrm = len(mesh.normals)

    def emit(pi: int, prim_i: int, corner: int) -> int:
        if 0 <= pi < n_pos:
            x, y, z = mesh.positions[pi]
        else:
            x, y, z = (0.0, 0.0, 0.0)
        if 0 <= pi < n_nrm:
            nx, ny, nz = mesh.normals[pi]
        else:
            nx, ny, nz = (0.0, 0.0, 1.0)
        u, v = _corner_uv(mesh, prim_i, corner)
        # Z-up -> Y-up: (x, y, z) -> (x, z, -y)
        positions.append((x, z, -y))
        normals.append((nx, nz, -ny))
        # PS2 V is already top-down (matches glTF (0,0)=top-left); no flip.
        uvs.append((u, v))
        return len(positions) - 1

    for i, (s0, s1, s2, s3) in enumerate(mesh.primitives):
        if not all(0 <= ix < n_pos for ix in (s0, s1, s2, s3)):
            continue
        # Skip palette-coloured / stub-UV prims: they have no texture binding
        # and would otherwise be assigned page 0 with all-zero UVs, producing
        # criss-crossed garbage in the textured material.
        e_idx = mesh.prim_uv_indices[i][0] if i < len(mesh.prim_uv_indices) else 0xFFFF
        if e_idx >= 0x8000 or e_idx >= len(mesh.uv_records):
            # Bucket into page=-1 (untextured / palette colour group).
            page = -1
        else:
            page = _prim_tex_page(mesh, i)
        idx_list = groups.setdefault(page, [])
        if s2 == s3:
            # Triangle (s0, s1, s2)
            a = emit(s0, i, 0)
            b = emit(s1, i, 1)
            c = emit(s2, i, 2)
            idx_list.extend([a, b, c])
        else:
            # PSM2 quads have cyclic vertex order around the perimeter
            # (s0->s1->s2->s3 traces the rectangle). Triangulate via
            # the s0-s2 diagonal so both halves share consistent winding:
            #   tri1 = (s0, s1, s2)
            #   tri2 = (s0, s2, s3)
            a = emit(s0, i, 0)
            b = emit(s1, i, 1)
            c = emit(s2, i, 2)
            d = emit(s3, i, 3)
            idx_list.extend([a, b, c])
            idx_list.extend([a, c, d])

    return positions, normals, uvs, groups


# ---------------------------------------------------------------------------
# glTF assembly
# ---------------------------------------------------------------------------

def write_gltf(mesh: PSM2Mesh, gltf_path: str, name: str,
               bundle_dir: Optional[str] = None,
               png_override: Optional[str] = None) -> dict:
    binbuf = _BinBuf()
    bin_path = os.path.splitext(gltf_path)[0] + ".bin"
    bin_uri = os.path.basename(bin_path)

    positions, normals, uvs, groups = _build_corners(mesh)
    if not positions:
        gltf: dict = {
            "asset": {"version": "2.0", "generator": "psm2_gltf.py"},
            "scene": 0,
            "scenes": [{"nodes": []}],
        }
        with open(gltf_path, 'w', encoding='utf-8') as fg:
            json.dump(gltf, fg, indent=2)
        return {
            'positions': 0, 'indices': 0, 'bin_bytes': 0,
            'gltf_path': gltf_path, 'bin_path': None,
            'preferred_png': None, 'pngs': [],
        }

    # ---- Texture page resolution (one PNG per group) ------------------
    pngs = _bundle_pngs(bundle_dir) if bundle_dir else []
    adjacency_list = _adjacency_pngs(name, pngs, bundle_dir)
    # png_override forces a single texture for every group.
    if png_override:
        forced_png: Optional[str] = png_override
    else:
        forced_png = None

    def page_to_png(page: int) -> Optional[str]:
        if forced_png:
            return forced_png
        if 0 <= page < len(adjacency_list):
            return adjacency_list[page] or None
        return None

    # ---- Vertex attribute buffer views --------------------------------
    pos_off = binbuf.append(_f32_vec3(positions))
    nrm_off = binbuf.append(_f32_vec3(normals))
    uv_off = binbuf.append(_f32_vec2(uvs))

    buffer_views: List[dict] = [
        {"buffer": 0, "byteOffset": pos_off, "byteLength": len(positions) * 12,
         "target": 34962},
        {"buffer": 0, "byteOffset": nrm_off, "byteLength": len(normals) * 12,
         "target": 34962},
        {"buffer": 0, "byteOffset": uv_off, "byteLength": len(uvs) * 8,
         "target": 34962},
    ]

    mn, mx = _bbox(positions)
    accessors: List[dict] = [
        {"bufferView": 0, "componentType": 5126, "count": len(positions),
         "type": "VEC3", "min": mn, "max": mx},
        {"bufferView": 1, "componentType": 5126, "count": len(normals),
         "type": "VEC3"},
        {"bufferView": 2, "componentType": 5126, "count": len(uvs),
         "type": "VEC2"},
    ]

    # ---- Per-group materials/textures/images --------------------------
    materials: List[dict] = []
    textures: List[dict] = []
    images: List[dict] = []
    samplers: List[dict] = []
    png_to_image_idx: Dict[str, int] = {}
    bound_pngs: List[str] = []

    def get_or_add_material(png: Optional[str], page: int) -> int:
        if not png:
            mat_idx = len(materials)
            materials.append({
                "name": f"psm2_page{page}_untextured",
                "pbrMetallicRoughness": {
                    "baseColorFactor": [0.5, 0.5, 0.5, 1.0],
                    "metallicFactor": 0.0,
                    "roughnessFactor": 1.0,
                },
                "doubleSided": True,
            })
            return mat_idx
        if png not in png_to_image_idx:
            # Copy the PNG next to the .gltf so the URI resolves.
            if bundle_dir:
                src_png = os.path.join(bundle_dir, png)
                dst_png = os.path.join(os.path.dirname(gltf_path), png)
                try:
                    if os.path.abspath(src_png) != os.path.abspath(dst_png):
                        with open(src_png, 'rb') as fi, open(dst_png, 'wb') as fo:
                            fo.write(fi.read())
                except OSError:
                    pass
            img_i = len(images)
            images.append({"uri": png, "name": png})
            if not samplers:
                samplers.append({"magFilter": 9729, "minFilter": 9987,
                                 "wrapS": 10497, "wrapT": 10497})
            tex_i = len(textures)
            textures.append({"source": img_i, "sampler": 0})
            png_to_image_idx[png] = tex_i
            bound_pngs.append(png)
        tex_i = png_to_image_idx[png]
        mat_idx = len(materials)
        materials.append({
            "name": f"psm2_page{page}_{png}",
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": tex_i},
                "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                "metallicFactor": 0.0,
                "roughnessFactor": 1.0,
            },
            "doubleSided": True,
        })
        return mat_idx

    primitives_json: List[dict] = []
    total_indices = 0
    for page in sorted(groups.keys()):
        idx_list = groups[page]
        if not idx_list:
            continue
        idx_off = binbuf.append(_u32_idx(idx_list))
        bv_i = len(buffer_views)
        buffer_views.append({
            "buffer": 0, "byteOffset": idx_off,
            "byteLength": len(idx_list) * 4, "target": 34963,
        })
        acc_i = len(accessors)
        accessors.append({
            "bufferView": bv_i, "componentType": 5125,
            "count": len(idx_list), "type": "SCALAR",
        })
        png = page_to_png(page)
        mat_idx = get_or_add_material(png, page)
        primitives_json.append({
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
            "indices": acc_i,
            "material": mat_idx,
            "mode": 4,  # TRIANGLES
        })
        total_indices += len(idx_list)

    if not primitives_json:
        # Fall back: no groups produced any geometry.
        materials.append({
            "name": "psm2_empty",
            "pbrMetallicRoughness": {
                "baseColorFactor": [0.5, 0.5, 0.5, 1.0],
                "metallicFactor": 0.0,
                "roughnessFactor": 1.0,
            },
            "doubleSided": True,
        })

    bin_blob = binbuf.bytes()
    with open(bin_path, 'wb') as fb:
        fb.write(bin_blob)

    gltf = {
        "asset": {"version": "2.0", "generator": "psm2_gltf.py"},
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
        'positions': len(positions),
        'indices': total_indices,
        'bin_bytes': len(bin_blob),
        'gltf_path': gltf_path,
        'bin_path': bin_path,
        'preferred_png': bound_pngs[0] if bound_pngs else None,
        'pngs': bound_pngs,
        'pages': sorted(groups.keys()),
    }


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def export_file(src_path: str, dst_dir: str, verbose: bool = False,
                png_override: Optional[str] = None) -> Optional[dict]:
    data = open(src_path, 'rb').read()
    if len(data) < 4 or _u32(data, 0) != MAGIC_PSM2:
        if verbose:
            print(f"[skip] {src_path}: not PSM2")
        return None
    os.makedirs(dst_dir, exist_ok=True)
    base = os.path.splitext(os.path.basename(src_path))[0]
    try:
        mesh = parse_psm2(data)
    except Exception as e:
        if verbose:
            print(f"[err]  {src_path}: {e}")
        return None
    gltf_path = os.path.join(dst_dir, base + ".gltf")
    bundle_dir = os.path.dirname(os.path.abspath(src_path))
    stats = write_gltf(mesh, gltf_path, name=base,
                       bundle_dir=bundle_dir,
                       png_override=png_override)
    if verbose:
        print(f"[ok]   {gltf_path}  verts={stats['positions']}  "
              f"idx={stats['indices']}  bin={stats['bin_bytes']}B  "
              f"pages={stats.get('pages')}  pngs={stats.get('pngs')}")
    return stats


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="PSM2 -> glTF 2.0 emitter")
    ap.add_argument('--src', required=True, help="PSM2 file or directory")
    ap.add_argument('--dst', required=True, help="Output directory")
    ap.add_argument('--limit', type=int, default=None)
    ap.add_argument('-v', '--verbose', action='store_true')
    ap.add_argument('--png', default=None,
                    help="Override the auto-picked BMPA PNG basename "
                         "(must exist in the same directory as --src).")
    args = ap.parse_args(argv)

    inputs: List[str] = []
    if os.path.isdir(args.src):
        for fn in sorted(os.listdir(args.src)):
            full = os.path.join(args.src, fn)
            if os.path.isfile(full) and fn.lower().endswith(('.psm2', '.bin')):
                inputs.append(full)
    else:
        inputs.append(args.src)
    if args.limit is not None:
        inputs = inputs[:args.limit]

    ok = 0
    for p in inputs:
        if export_file(p, args.dst, verbose=args.verbose,
                       png_override=args.png):
            ok += 1
    print(f"Wrote glTF for {ok}/{len(inputs)} PSM2 file(s) into {args.dst}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
