#!/usr/bin/env python3
"""PSM2 parser/exporter — v2 (clean implementation).

Code-derived semantics (verified against src/FUN_0022b5a8.c and the post-load
passes FUN_0022c6e8 / FUN_00211230):

- Section C (header +0x08): per-vertex POSITIONS. Layout per record:
    +0x00..+0x0B : three IEEE-754 floats (x, y, z) — used directly by
                   FUN_0022c6e8 for AABB and face normal computation.
    +0x0C..+0x0D : u16 b_index (index into Section B records).
    +0x0E        : u8  style flags (used by renderer to pick draw mode).

- Section B (header +0x30): per-vertex NORMALS as unit float triplets.
  Each record is 4 dwords (3 floats + 1 zero pad), 0x10 bytes. The renderer
  multiplies by 126.0 and packs to signed bytes — only consistent with unit
  vectors, and inspection of real maps confirms range [-1, 1] with exact
  axis-aligned values like (0,1,0), (0,0,1), (-1,0,0).

- Section A (header +0x04): per-mesh records, 0x20 stride, six dwords each.
  Used by Section J construction; not directly relevant for offline mesh
  extraction.

- Section D (header +0x0C): primitives. Each on-disk record is exactly
  16 u16 (32 bytes). Only the first four matter for geometry:
    u16[0..3] : four C-indices forming a triangle (when [2]==[3]) or a quad.

- Quad triangulation matches FUN_0022c6e8's normal computation calls:
    triangle  : (s0, s1, s2)
    quad      : (s3, s0, s1) and (s1, s2, s3)

Notes:
- FUN_0022b4e0 / FUN_0022b520 are trivial read-u32-and-advance / read-u16
  helpers, NOT variable-length readers.
- The "extra A triplet" appended to per-J vertex buffers at runtime is a
  difference vector used internally; it does not extend the C index domain
  for D-records, so we ignore it for OBJ export.
"""
from __future__ import annotations

import argparse
import os
import struct
from dataclasses import dataclass, field
from typing import List, Tuple

MAGIC_PSM2 = 0x324D5350


@dataclass
class PSM2Mesh:
    """In-memory representation of one PSM2 chunk."""
    positions: List[Tuple[float, float, float]] = field(default_factory=list)
    """Section C float triplets (vertex positions)."""

    normals: List[Tuple[float, float, float]] = field(default_factory=list)
    """Section B float triplets (per-position-index normal, via c_to_b map)."""

    primitives: List[Tuple[int, int, int, int]] = field(default_factory=list)
    """Section D first-four-indices per record. (s0, s1, s2, s3) with
    s2 == s3 indicating a triangle."""

    c_to_b: List[int] = field(default_factory=list)
    """Per-position index into Section B (one entry per Section C record)."""

    header_offsets: dict = field(default_factory=dict)


def _u16(buf: bytes, off: int) -> int:
    return struct.unpack_from('<H', buf, off)[0]


def _s16(buf: bytes, off: int) -> int:
    return struct.unpack_from('<h', buf, off)[0]


def _u32(buf: bytes, off: int) -> int:
    return struct.unpack_from('<I', buf, off)[0]


def _f32(buf: bytes, off: int) -> float:
    return struct.unpack_from('<f', buf, off)[0]


def parse_psm2(buf: bytes) -> PSM2Mesh:
    if len(buf) < 0x3C or _u32(buf, 0) != MAGIC_PSM2:
        raise ValueError("not a PSM2 chunk")

    offs = {
        'A': _u32(buf, 0x04),
        'C': _u32(buf, 0x08),
        'D': _u32(buf, 0x0C),
        'B': _u32(buf, 0x30),
    }
    mesh = PSM2Mesh(header_offsets=offs)

    # Section C — positions (+ b_index per vertex)
    if offs['C']:
        base = offs['C']
        cnt = _s16(buf, base)
        p = base + 2
        for _ in range(max(0, cnt)):
            x, y, z = _f32(buf, p), _f32(buf, p + 4), _f32(buf, p + 8)
            b_index = _u16(buf, p + 12)
            mesh.positions.append((x, y, z))
            mesh.c_to_b.append(b_index)
            p += 16

    # Section B — normals. The on-disk layout is:
    #   +0  : u32 count (the loader truncates this to a signed short)
    #   +4  : 3 dwords (= 3 floats) per record, no padding in the file
    # In memory each record is widened to 0x10 bytes by zeroing a 4th dword,
    # but on disk the stride is 12 bytes.
    bvals: List[Tuple[float, float, float]] = []
    if offs['B']:
        base = offs['B']
        cnt = _u32(buf, base) & 0xFFFF
        # Treat as signed short — negative means "no records".
        if cnt >= 0x8000:
            cnt = 0
        p = base + 4
        for _ in range(cnt):
            if p + 12 > len(buf):
                break
            bvals.append((_f32(buf, p), _f32(buf, p + 4), _f32(buf, p + 8)))
            p += 12

    # Resolve per-position normals through the C->B map.
    for bi in mesh.c_to_b:
        if 0 <= bi < len(bvals):
            mesh.normals.append(bvals[bi])
        else:
            mesh.normals.append((0.0, 0.0, 1.0))

    # Section D — primitives (32 bytes per record; first 4 u16 are indices).
    # The loader reads `count` from the section start, but records begin 4
    # bytes in (`&DAT_01849a04 + iVar22` in FUN_0022b5a8), not 2 bytes in —
    # the 2-byte slot after the count is reserved/unused header.
    if offs['D']:
        base = offs['D']
        cnt = _s16(buf, base)
        p = base + 4
        for _ in range(max(0, cnt)):
            if p + 8 > len(buf):
                break
            mesh.primitives.append((
                _u16(buf, p + 0),
                _u16(buf, p + 2),
                _u16(buf, p + 4),
                _u16(buf, p + 6),
            ))
            p += 32

    return mesh


def find_psm2_offsets(buf: bytes) -> List[int]:
    """Locate every PSM2 magic in `buf` (whole-file or embedded)."""
    out: List[int] = []
    pos = 0
    needle = MAGIC_PSM2.to_bytes(4, 'little')
    while True:
        i = buf.find(needle, pos)
        if i == -1:
            break
        if _u32(buf, i) == MAGIC_PSM2:
            out.append(i)
        pos = i + 1
    return out


def write_obj(mesh: PSM2Mesh, path: str, name: str = "mesh") -> Tuple[int, int]:
    """Write one mesh to an OBJ file. Returns (vertex_count, face_count)."""
    n_verts = len(mesh.positions)
    n_faces = 0
    with open(path, 'w', encoding='utf-8') as f:
        f.write(f"# PSM2 mesh: {name}\n")
        f.write(f"# verts={n_verts} normals={len(mesh.normals)} "
                f"primitives={len(mesh.primitives)}\n")
        f.write(f"o {name}\n")
        # PSM2 stores vertices in a Z-up, right-handed frame (the in-engine
        # camera/world setup uses Z as the vertical axis). OBJ viewers
        # conventionally interpret OBJ as Y-up, so remap (x, y, z) → (x, z, -y)
        # which preserves handedness while making "up" line up with the viewer.
        for x, y, z in mesh.positions:
            f.write(f"v {x:.6f} {z:.6f} {-y:.6f}\n")
        for nx, ny, nz in mesh.normals:
            f.write(f"vn {nx:.6f} {nz:.6f} {-ny:.6f}\n")
        # Faces — OBJ is 1-based.
        for s0, s1, s2, s3 in mesh.primitives:
            # Bounds-check; skip primitives that reference indices outside C.
            if not all(0 <= ix < n_verts for ix in (s0, s1, s2, s3)):
                continue
            if s2 == s3:
                # Triangle, winding (s0, s1, s2)
                a, b, c = s0 + 1, s1 + 1, s2 + 1
                f.write(f"f {a}//{a} {b}//{b} {c}//{c}\n")
                n_faces += 1
            else:
                # Quad split as (s3, s0, s1) + (s1, s2, s3)
                a, b, c, d = s0 + 1, s1 + 1, s2 + 1, s3 + 1
                f.write(f"f {d}//{d} {a}//{a} {b}//{b}\n")
                f.write(f"f {b}//{b} {c}//{c} {d}//{d}\n")
                n_faces += 2
    return n_verts, n_faces


def export_file(input_path: str, dst_dir: str, verbose: bool = False) -> List[str]:
    """Extract every PSM2 chunk in `input_path` and write each as a separate OBJ."""
    data = open(input_path, 'rb').read()
    offsets: List[int] = []
    if len(data) >= 4 and _u32(data, 0) == MAGIC_PSM2:
        offsets.append(0)
    for o in find_psm2_offsets(data):
        if o not in offsets:
            offsets.append(o)
    if not offsets:
        if verbose:
            print(f"[skip] {input_path}: no PSM2 magic")
        return []

    os.makedirs(dst_dir, exist_ok=True)
    base = os.path.splitext(os.path.basename(input_path))[0]
    written: List[str] = []
    for off in offsets:
        try:
            mesh = parse_psm2(data[off:])
        except Exception as e:
            if verbose:
                print(f"[err]  {input_path} @0x{off:X}: {e}")
            continue
        suffix = f"_ofs{off:X}" if off else ""
        out_path = os.path.join(dst_dir, f"{base}{suffix}.obj")
        nv, nf = write_obj(mesh, out_path, name=f"{base}{suffix}")
        written.append(out_path)
        if verbose:
            print(f"[ok]   {out_path} verts={nv} faces={nf}")
    return written


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="PSM2 mesh extractor (v2)")
    ap.add_argument('--src', required=True,
                    help="Source file or directory containing PSM2 chunks.")
    ap.add_argument('--dst', required=True, help="Output directory for OBJ files.")
    ap.add_argument('--limit', type=int, default=None,
                    help="Maximum number of source files to process.")
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

    total_files = 0
    total_objs = 0
    for path in inputs:
        outs = export_file(path, args.dst, verbose=args.verbose)
        if outs:
            total_files += 1
            total_objs += len(outs)

    print(f"Processed {len(inputs)} file(s); "
          f"{total_files} contained PSM2 ({total_objs} OBJ chunks written) "
          f"into {args.dst}")
    return 0


if __name__ == '__main__':
    import sys
    raise SystemExit(main(sys.argv[1:]))
