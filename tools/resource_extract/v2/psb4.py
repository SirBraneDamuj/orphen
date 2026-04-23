#!/usr/bin/env python3
"""PSB4 mesh extractor — v2 (clean implementation).

Code-derived semantics, verified against:
- src/FUN_0022ce60.c   (loader; validates magic 'PSB4' = 0x34425350,
                        unpacks Sections A/B/C into the GS scratch ring)
- src/FUN_0022cd88.c   (caller — loads file via FUN_00223268 type=2)
- src/FUN_002256d0.c   (Section C runtime size: 16 + ((n-1)*10 + 3 & ~3))

File layout (file-relative):
  +0x00 : magic 'PSB4'
  +0x04 : u32 offs_section_A (positions)
  +0x08 : u32 offs_section_B (faces / shaded prims)
  +0x0c : u32 offs_section_C (auxiliary strip data — collision / shadow LOD)

Section A (positions):
  +0x00 : s16 count
  +0x02 : 2 bytes pad (reserved header — same convention as PSM2 Section D)
  records: stride 12 = 3 × f32  (x, y, z)
  In memory the loader expands each to 16 bytes (3 dwords + 0 W pad).

Section B (faces / shaded primitives):
  +0x00 : s16 count
  +0x02 : 2 bytes pad
  records: stride 22 bytes
    +0x00 : u16  flags  (bit 0x800 selects per-vertex UV vs per-vertex RGB)
    +0x02 : 4 × s16 vertex indices into Section A
                    (v3 == v2 → triangle, otherwise quad)
    +0x0a : 12 bytes per-vertex attributes:
              flags & 0x800 == 0 → 4 × (R, G, B) bytes (unlit vertex color)
              flags & 0x800 != 0 → 4 × (U, V, ?) bytes (UV + 3rd byte)

Section C (auxiliary strips — likely collision / shadow):
  +0x00 : s16 strip_count
  per strip: 1 + 3*sub_count shorts
    +0    : s16 sub_count
    +2..  : sub_count × 3 × s16  (triangle indices into Section A)
  Not emitted into OBJ by default — different topology and unclear UV mapping.
  Use --include-aux to dump them as a second object.

Triangle / quad winding follows the PSM2 / PSC3 convention:
  triangle: (v0, v1, v2)
  quad:     (v3, v0, v1) and (v1, v2, v3)
"""
from __future__ import annotations

import argparse
import os
import struct
from dataclasses import dataclass, field
from typing import List, Tuple

MAGIC_PSB4 = 0x34425350


@dataclass
class PSB4Mesh:
    positions: List[Tuple[float, float, float]] = field(default_factory=list)
    primitives: List[Tuple[int, int, int, int]] = field(default_factory=list)
    aux_triangles: List[Tuple[int, int, int]] = field(default_factory=list)
    header: dict = field(default_factory=dict)


def _u16(b: bytes, o: int) -> int:
    return struct.unpack_from('<H', b, o)[0]


def _s16(b: bytes, o: int) -> int:
    return struct.unpack_from('<h', b, o)[0]


def _u32(b: bytes, o: int) -> int:
    return struct.unpack_from('<I', b, o)[0]


def parse_psb4(buf: bytes) -> PSB4Mesh:
    if len(buf) < 0x10 or _u32(buf, 0) != MAGIC_PSB4:
        raise ValueError("not a PSB4 buffer")
    o_a = _u32(buf, 4)
    o_b = _u32(buf, 8)
    o_c = _u32(buf, 0x0C)
    mesh = PSB4Mesh()
    mesh.header = {'offs_a': o_a, 'offs_b': o_b, 'offs_c': o_c}

    # Section A — positions (3 × f32 per record after the 4-byte header)
    if o_a:
        n = _s16(buf, o_a)
        for i in range(n):
            base = o_a + 4 + i * 12
            x, y, z = struct.unpack_from('<fff', buf, base)
            mesh.positions.append((x, y, z))

    # Section B — face records, stride 22
    if o_b:
        n = _s16(buf, o_b)
        for i in range(n):
            base = o_b + 4 + i * 22
            if base + 22 > len(buf):
                break
            v0 = _u16(buf, base + 2)
            v1 = _u16(buf, base + 4)
            v2 = _u16(buf, base + 6)
            v3 = _u16(buf, base + 8)
            mesh.primitives.append((v0, v1, v2, v3))

    # Section C — strip-list. 1 short sub_count then sub_count*3 shorts.
    if o_c:
        sc = _s16(buf, o_c)
        cur = o_c + 2
        for _ in range(sc):
            if cur + 2 > len(buf):
                break
            sub = _s16(buf, cur)
            cur += 2
            for _t in range(sub):
                if cur + 6 > len(buf):
                    break
                a, b, c = struct.unpack_from('<hhh', buf, cur)
                cur += 6
                mesh.aux_triangles.append((a, b, c))

    return mesh


def write_obj(mesh: PSB4Mesh, path: str, name: str = "mesh",
              include_aux: bool = False) -> Tuple[int, int]:
    n_verts = len(mesh.positions)
    n_faces = 0
    with open(path, 'w', encoding='utf-8') as f:
        f.write(f"# PSB4 mesh: {name}\n")
        f.write(f"# verts={n_verts} primitives={len(mesh.primitives)} "
                f"aux_tris={len(mesh.aux_triangles)}\n")
        f.write(f"o {name}\n")
        # PSB4 (like PSM2 / PSC3) is Z-up; remap to Y-up: (x, y, z) -> (x, z, -y).
        for x, y, z in mesh.positions:
            f.write(f"v {x:.6f} {z:.6f} {-y:.6f}\n")
        for v0, v1, v2, v3 in mesh.primitives:
            if not all(0 <= ix < n_verts for ix in (v0, v1, v2, v3)):
                continue
            if v2 == v3:
                f.write(f"f {v0+1} {v1+1} {v2+1}\n")
                n_faces += 1
            else:
                f.write(f"f {v3+1} {v0+1} {v1+1}\n")
                f.write(f"f {v1+1} {v2+1} {v3+1}\n")
                n_faces += 2
        if include_aux and mesh.aux_triangles:
            f.write(f"o {name}_aux\n")
            for a, b, c in mesh.aux_triangles:
                if all(0 <= ix < n_verts for ix in (a, b, c)):
                    f.write(f"f {a+1} {b+1} {c+1}\n")
                    n_faces += 1
    return n_verts, n_faces


def export_file(input_path: str, dst_dir: str, verbose: bool = False,
                include_aux: bool = False) -> List[str]:
    """Extract a PSB4 chunk if the magic is at offset 0.

    The loader (FUN_0022ce60) only reads from the start of the load buffer —
    embedded 'PSB4' bytes elsewhere are not real headers.
    """
    data = open(input_path, 'rb').read()
    if len(data) < 4 or _u32(data, 0) != MAGIC_PSB4:
        if verbose:
            print(f"[skip] {input_path}: no PSB4 magic at offset 0")
        return []
    os.makedirs(dst_dir, exist_ok=True)
    base = os.path.splitext(os.path.basename(input_path))[0]
    try:
        mesh = parse_psb4(data)
    except Exception as e:
        if verbose:
            print(f"[err]  {input_path}: {e}")
        return []
    out_path = os.path.join(dst_dir, f"{base}.obj")
    nv, nf = write_obj(mesh, out_path, name=base, include_aux=include_aux)
    if verbose:
        print(f"[ok]   {out_path} verts={nv} faces={nf}")
    return [out_path]


def main(argv: List[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="PSB4 -> OBJ extractor (v2)")
    ap.add_argument('--src', required=True, help="source file or directory")
    ap.add_argument('--dst', required=True, help="output directory")
    ap.add_argument('--limit', type=int, default=0,
                    help="process at most N files (0 = unlimited)")
    ap.add_argument('--include-aux', action='store_true',
                    help="also emit Section C triangles as a second object")
    ap.add_argument('--verbose', action='store_true')
    args = ap.parse_args(argv)

    if os.path.isdir(args.src):
        files = sorted(os.path.join(args.src, n) for n in os.listdir(args.src)
                       if os.path.isfile(os.path.join(args.src, n)))
    else:
        files = [args.src]
    if args.limit:
        files = files[:args.limit]

    written_total = 0
    psb4_files = 0
    for p in files:
        out = export_file(p, args.dst, verbose=args.verbose,
                          include_aux=args.include_aux)
        if out:
            psb4_files += 1
            written_total += len(out)
    print(f"Processed {len(files)} file(s); {psb4_files} contained PSB4 "
          f"({written_total} OBJ chunks written) into {args.dst}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
