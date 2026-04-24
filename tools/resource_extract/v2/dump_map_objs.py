"""Convert every extracted MAP.BIN mesh into an OBJ.

Usage:
    python -m tools.resource_extract.v2.dump_map_objs <SRC_DIR> <DST_DIR>

SRC_DIR is expected to look like `out/all/map/` with files named
`NNNN.psm2`, `NNNN.psc3`, or `NNNN.psb4`.

Writes `NNNN_PSM2.obj`, `NNNN_PSC3.obj`, `NNNN_PSB4.obj` (skipping meshes
that yielded no geometry) plus a short stats dump at the end.

All three parsers are the pristine v2 implementations — they already validate
against MAP.BIN output of the game's own loaders.
"""
from __future__ import annotations

import argparse
import os
import sys
from collections import Counter

from . import psm2 as psm2_mod
from . import psc3 as psc3_mod
from . import psb4 as psb4_mod


def _write_obj(path: str, name: str,
               positions, normals, primitives) -> bool:
    if not positions or not primitives:
        return False
    with open(path, "w") as f:
        f.write(f"# {name}\n")
        f.write(f"# v={len(positions)} n={len(normals)} f={len(primitives)}\n")
        f.write(f"o {name}\n")
        for x, y, z in positions:
            # Remap PS2 (x, y, z) -> (x, z, -y) so Z-up becomes Y-up.
            f.write(f"v {x:.6f} {z:.6f} {-y:.6f}\n")
        for nx, ny, nz in normals:
            f.write(f"vn {nx:.6f} {nz:.6f} {-ny:.6f}\n")
        for prim in primitives:
            # Triangle strip style: prims with 3 unique indices -> tri
            # (v0, v1, v2, v3); if v2 == v3 we have a triangle.
            a, b, c, *_ = prim
            d = prim[3] if len(prim) > 3 else c
            if c == d:
                f.write(f"f {a+1} {b+1} {c+1}\n")
            else:
                f.write(f"f {a+1} {b+1} {c+1} {d+1}\n")
    return True


def run(src: str, dst: str) -> None:
    os.makedirs(dst, exist_ok=True)
    stats = Counter()
    for fn in sorted(os.listdir(src)):
        path = os.path.join(src, fn)
        stem, ext = os.path.splitext(fn)
        buf = open(path, "rb").read()
        out_name = None
        positions = normals = primitives = None
        try:
            if ext == ".psm2":
                mesh = psm2_mod.parse_psm2(buf)
                positions, normals, primitives = mesh.positions, mesh.normals, mesh.primitives
                out_name = f"{stem}_PSM2.obj"
            elif ext == ".psc3":
                mesh = psc3_mod.parse_psc3(buf)
                positions, normals, primitives = mesh.positions, mesh.normals, mesh.primitives
                out_name = f"{stem}_PSC3.obj"
            elif ext == ".psb4":
                mesh = psb4_mod.parse_psb4(buf)
                positions = mesh.positions
                normals = []  # PSB4 has no vertex normals
                primitives = mesh.primitives
                out_name = f"{stem}_PSB4.obj"
            else:
                continue
        except Exception as ex:
            stats[f"{ext}_fail"] += 1
            continue
        if not out_name:
            continue
        if _write_obj(os.path.join(dst, out_name),
                      out_name.replace(".obj", ""),
                      positions, normals, primitives):
            stats[f"{ext}_ok"] += 1
        else:
            stats[f"{ext}_empty"] += 1
    print(f"\nWrote OBJs to {dst}")
    for k in sorted(stats):
        print(f"  {k:<12} {stats[k]}")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Render all MAP.BIN meshes as OBJ")
    ap.add_argument("src", default="out/all/map", nargs="?",
                    help="Directory with .psm2/.psc3/.psb4 files")
    ap.add_argument("dst", default="out/all/map_obj", nargs="?",
                    help="Output directory for OBJs")
    args = ap.parse_args(argv)
    run(args.src, args.dst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
