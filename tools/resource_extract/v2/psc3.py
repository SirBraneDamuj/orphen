#!/usr/bin/env python3
"""PSC3 mesh extractor — v2 (clean implementation).

Code-derived semantics, verified against:
- src/FUN_00222498.c   (loader entry; validates magic 'PSC3' = 0x33435350)
- src/FUN_00212058.c   (renderer; iterates submeshes -> primitives -> subdraws)
- src/FUN_002129b8.c   (per-vertex stream builder — settles the field layout)

Header layout (all offsets are file-relative):
  +0x00 : magic 'PSC3'
  +0x04 : s16 submesh_count
  +0x08 : u32 offs_submeshes      — stride 0x14, each `short[+6] = max_prim_idx`
  +0x14 : u32 offs_vertices       — stride 10:
                                       +0..+5  : 3 × s16  (x, y, z)  scale = 1/2048
                                       +0x06   : u16 normal_index (into Section N)
                                       +0x08   : 2 bytes  (style/pad)
  +0x18 : u32 offs_vertex_bytes   — stride 1; per-vertex packed style/color
  +0x1c : u32 offs_primitives     — stride 0x18:
                                       +0x00..+0x06 : 4 × s16 vertex indices
                                                      (v2 == v3 -> triangle)
                                       +0x08        : u16 flags (0x20 = skip)
                                       +0x0c        : u8  primary subdraw idx
                                       +0x0d        : u8  alpha/state byte
                                       +0x0e..+0x14 : 4 × s16 subdraw mat indices
                                                      (-1 means "no draw")
  +0x20 : u32 offs_colors          — stride 3 (per-color rgb byte triplet)
  +0x24 : u32 offs_materials       — stride 10
  +0x28 : u32 offs_normals         — stride 0x10: 3 × f32 + pad
  +0x40 : optional 4-LUT remap table (FUN_00221e70)

Triangle / quad winding (matches PSM2 derivation):
  triangle (v2 == v3): (v0, v1, v2)
  quad              : (v3, v0, v1) and (v1, v2, v3)

The renderer emits up to 4 separate draws per primitive (one per non-(-1)
subdraw mat index at +0x0e..+0x14); these are *the same triangle/quad*
re-rendered with different material/blend setups. For offline geometry
extraction we emit each primitive once.

Vertex counts are inferred from the gap between sequential section offsets:
- vertex_count  = (offs_vertex_bytes - offs_vertices) / 10
- normal_count  = inferred similarly when offs_normals is followed by another
                  known section, otherwise scanned from primitive references.
"""
from __future__ import annotations

import argparse
import os
import struct
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

MAGIC_PSC3 = 0x33435350
POS_SCALE = 1.0 / 2048.0  # FUN_002129b8: short * 0.00048828125


@dataclass
class PSC3Mesh:
    positions: List[Tuple[float, float, float]] = field(default_factory=list)
    normals: List[Tuple[float, float, float]] = field(default_factory=list)
    vertex_normal_idx: List[int] = field(default_factory=list)
    primitives: List[Tuple[int, int, int, int]] = field(default_factory=list)
    header: dict = field(default_factory=dict)


def _u16(buf: bytes, off: int) -> int:
    return struct.unpack_from('<H', buf, off)[0]


def _s16(buf: bytes, off: int) -> int:
    return struct.unpack_from('<h', buf, off)[0]


def _u32(buf: bytes, off: int) -> int:
    return struct.unpack_from('<I', buf, off)[0]


def _f32(buf: bytes, off: int) -> float:
    return struct.unpack_from('<f', buf, off)[0]


def parse_psc3(buf: bytes) -> PSC3Mesh:
    if len(buf) < 0x44 or _u32(buf, 0) != MAGIC_PSC3:
        raise ValueError("not a PSC3 chunk")

    h = {
        'submesh_count':    _s16(buf, 0x04),
        'offs_submeshes':   _u32(buf, 0x08),
        'offs_vertices':    _u32(buf, 0x14),
        'offs_vertex_bytes': _u32(buf, 0x18),
        'offs_primitives':  _u32(buf, 0x1C),
        'offs_colors':      _u32(buf, 0x20),
        'offs_materials':   _u32(buf, 0x24),
        'offs_normals':     _u32(buf, 0x28),
    }
    mesh = PSC3Mesh(header=h)

    # ------------------------------------------------------------------
    # Find the per-submesh "primitive count" by scanning short[+6].
    # The renderer iterates max(prim_count) times across the primitive table.
    # ------------------------------------------------------------------
    prim_count = 0
    if h['offs_submeshes'] and h['submesh_count'] > 0:
        for i in range(h['submesh_count']):
            base = h['offs_submeshes'] + i * 0x14
            if base + 8 > len(buf):
                break
            v = _s16(buf, base + 6)
            if v > prim_count:
                prim_count = v

    # ------------------------------------------------------------------
    # Vertex table. Stride 10. Count is inferred from the gap to the
    # following section (either +0x18 vertex-byte table or whatever comes
    # next in offset order).
    # ------------------------------------------------------------------
    if h['offs_vertices']:
        section_offsets = sorted(o for o in (
            h['offs_vertex_bytes'], h['offs_primitives'], h['offs_colors'],
            h['offs_materials'], h['offs_normals'], h['offs_submeshes'],
        ) if o and o > h['offs_vertices'])
        end = section_offsets[0] if section_offsets else len(buf)
        v_count = (end - h['offs_vertices']) // 10
        for i in range(v_count):
            p = h['offs_vertices'] + i * 10
            x = _s16(buf, p + 0) * POS_SCALE
            y = _s16(buf, p + 2) * POS_SCALE
            z = _s16(buf, p + 4) * POS_SCALE
            n_idx = _u16(buf, p + 6)
            mesh.positions.append((x, y, z))
            mesh.vertex_normal_idx.append(n_idx)

    # ------------------------------------------------------------------
    # Normal table. Stride 0x10, 3 floats + pad. Count is bounded by the
    # gap to the next section after offs_normals (or end of buffer).
    # ------------------------------------------------------------------
    n_table: List[Tuple[float, float, float]] = []
    if h['offs_normals']:
        later = sorted(o for o in (
            h['offs_vertices'], h['offs_vertex_bytes'], h['offs_primitives'],
            h['offs_colors'], h['offs_materials'], h['offs_submeshes'],
            _u32(buf, 0x40),
        ) if o and o > h['offs_normals'])
        end = later[0] if later else len(buf)
        n_count = (end - h['offs_normals']) // 0x10
        for i in range(n_count):
            p = h['offs_normals'] + i * 0x10
            if p + 12 > len(buf):
                break
            n_table.append((_f32(buf, p), _f32(buf, p + 4), _f32(buf, p + 8)))

    # Resolve per-vertex normals through the V->N map.
    for ni in mesh.vertex_normal_idx:
        if 0 <= ni < len(n_table):
            mesh.normals.append(n_table[ni])
        else:
            mesh.normals.append((0.0, 0.0, 1.0))

    # ------------------------------------------------------------------
    # Primitives. Stride 0x18; vertex indices at +0..+6; flag at +8.
    # We emit one primitive per record (skipping flag & 0x20), regardless
    # of how many subdraws the renderer would have done — geometry is the
    # same across subdraws.
    # ------------------------------------------------------------------
    if h['offs_primitives'] and prim_count > 0:
        for i in range(prim_count):
            base = h['offs_primitives'] + i * 0x18
            if base + 0x18 > len(buf):
                break
            flags = _u16(buf, base + 8)
            if flags & 0x20:
                continue  # renderer skips these
            # Require at least one valid subdraw mat index.
            valid_subdraw = False
            for k in range(4):
                if _s16(buf, base + 0x0E + k * 2) != -1:
                    valid_subdraw = True
                    break
            if not valid_subdraw:
                continue
            v0 = _u16(buf, base + 0)
            v1 = _u16(buf, base + 2)
            v2 = _u16(buf, base + 4)
            v3 = _u16(buf, base + 6)
            mesh.primitives.append((v0, v1, v2, v3))

    return mesh


def find_psc3_offsets(buf: bytes) -> List[int]:
    out: List[int] = []
    pos = 0
    needle = MAGIC_PSC3.to_bytes(4, 'little')
    while True:
        i = buf.find(needle, pos)
        if i == -1:
            break
        out.append(i)
        pos = i + 1
    return out


def write_obj(mesh: PSC3Mesh, path: str, name: str = "mesh") -> Tuple[int, int]:
    n_verts = len(mesh.positions)
    n_faces = 0
    with open(path, 'w', encoding='utf-8') as f:
        f.write(f"# PSC3 mesh: {name}\n")
        f.write(f"# verts={n_verts} normals={len(mesh.normals)} "
                f"primitives={len(mesh.primitives)}\n")
        f.write(f"o {name}\n")
        # PSC3 (like PSM2) is Z-up; remap to Y-up: (x, y, z) -> (x, z, -y).
        for x, y, z in mesh.positions:
            f.write(f"v {x:.6f} {z:.6f} {-y:.6f}\n")
        for nx, ny, nz in mesh.normals:
            f.write(f"vn {nx:.6f} {nz:.6f} {-ny:.6f}\n")
        for v0, v1, v2, v3 in mesh.primitives:
            if not all(0 <= ix < n_verts for ix in (v0, v1, v2, v3)):
                continue
            if v2 == v3:
                a, b, c = v0 + 1, v1 + 1, v2 + 1
                f.write(f"f {a}//{a} {b}//{b} {c}//{c}\n")
                n_faces += 1
            else:
                a, b, c, d = v0 + 1, v1 + 1, v2 + 1, v3 + 1
                f.write(f"f {d}//{d} {a}//{a} {b}//{b}\n")
                f.write(f"f {b}//{b} {c}//{c} {d}//{d}\n")
                n_faces += 2
    return n_verts, n_faces


def export_file(input_path: str, dst_dir: str, verbose: bool = False) -> List[str]:
    """Extract every PSC3 chunk in `input_path` and write each as an OBJ.

    Only validates magic at offset 0 — the loader (FUN_00222498) never scans
    inside the buffer for embedded magics. Searching for 'PSC3' anywhere in
    the file produces hundreds of false positives where those bytes appear
    naturally inside vertex / index streams.
    """
    data = open(input_path, 'rb').read()
    if len(data) < 4 or _u32(data, 0) != MAGIC_PSC3:
        if verbose:
            print(f"[skip] {input_path}: no PSC3 magic at offset 0")
        return []
    os.makedirs(dst_dir, exist_ok=True)
    base = os.path.splitext(os.path.basename(input_path))[0]
    try:
        mesh = parse_psc3(data)
    except Exception as e:
        if verbose:
            print(f"[err]  {input_path}: {e}")
        return []
    out_path = os.path.join(dst_dir, f"{base}.obj")
    nv, nf = write_obj(mesh, out_path, name=base)
    if verbose:
        print(f"[ok]   {out_path} verts={nv} faces={nf}")
    return [out_path]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="PSC3 mesh extractor (v2)")
    ap.add_argument('--src', required=True)
    ap.add_argument('--dst', required=True)
    ap.add_argument('--limit', type=int, default=None)
    ap.add_argument('--verbose', action='store_true')
    args = ap.parse_args(argv)

    inputs: List[str] = []
    if os.path.isdir(args.src):
        for name in sorted(os.listdir(args.src)):
            full = os.path.join(args.src, name)
            if os.path.isfile(full):
                inputs.append(full)
    else:
        inputs.append(args.src)
    if args.limit is not None:
        inputs = inputs[:args.limit]

    files = 0
    objs = 0
    for path in inputs:
        outs = export_file(path, args.dst, verbose=args.verbose)
        if outs:
            files += 1
            objs += len(outs)
    print(f"Processed {len(inputs)} file(s); {files} contained PSC3 "
          f"({objs} OBJ chunks written) into {args.dst}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
