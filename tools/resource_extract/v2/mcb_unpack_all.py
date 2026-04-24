"""End-to-end MCB bundle exploder.

Given an MCB1 bundle file (or a directory of them), unpack every record and
write the decoded artifact directly as a usable file:

  - PSM2 / PSC3 / PSB4 meshes  -> .obj (Y-up, same remap as dump_map_objs)
  - BMPA textures              -> .png (BGRA palette, flipped vertically)
  - everything else            -> raw .bin payload

Plus a `_manifest.txt` per bundle listing every record's id, category,
magic, decoded size, and the file written.

Usage:
    python -m tools.resource_extract.v2.mcb_unpack_all \
        --src out/all/mcb/s01_e011.bin --dst out/all/mcb_full/s01_e011

    # Or point at the whole directory (creates one subdir per bundle):
    python -m tools.resource_extract.v2.mcb_unpack_all \
        --src out/all/mcb --dst out/all/mcb_full

This is just glue around the existing modules:
    mcb_bundle.iter_records        (linked-list walker)
    baseline.lz_decoder.decode_bytes
    psm2 / psc3 / psb4             (mesh parsers)
    bmpa                           (texture parser + PNG writer)
"""
from __future__ import annotations

import argparse
import os
import sys
from collections import Counter
from pathlib import Path
from typing import Iterable

from ..baseline.lz_decoder import decode_bytes as lz_decode
from . import bmpa as bmpa_mod
from . import mcb_bundle
from . import psb4 as psb4_mod
from . import psc3 as psc3_mod
from . import psm2 as psm2_mod


CATEGORY_NAMES = mcb_bundle.CATEGORY_NAMES


# ---------------------------------------------------------------------------
# Mesh OBJ writer — copied verbatim from dump_map_objs._write_obj so the
# two tools produce byte-identical OBJs.
# ---------------------------------------------------------------------------

def _write_obj(path: str, name: str, positions, normals, primitives) -> bool:
    if not positions or not primitives:
        return False
    with open(path, "w") as f:
        f.write(f"# {name}\n")
        f.write(f"# v={len(positions)} n={len(normals)} f={len(primitives)}\n")
        f.write(f"o {name}\n")
        for x, y, z in positions:
            f.write(f"v {x:.6f} {z:.6f} {-y:.6f}\n")
        for nx, ny, nz in normals:
            f.write(f"vn {nx:.6f} {nz:.6f} {-ny:.6f}\n")
        for prim in primitives:
            a, b, c, *_ = prim
            d = prim[3] if len(prim) > 3 else c
            if c == d:
                f.write(f"f {a+1} {b+1} {c+1}\n")
            else:
                f.write(f"f {a+1} {b+1} {c+1} {d+1}\n")
    return True


def _classify_magic(buf: bytes) -> str:
    """Return one of psm2/psc3/psb4/bmpa/bin based on the decoded payload's
    4-byte ASCII magic. Matches mcb_bundle._classify (proven on 6,935 records)."""
    if len(buf) < 4:
        return "bin"
    m = buf[:4]
    if m == b"PSM2":
        return "psm2"
    if m == b"PSC3":
        return "psc3"
    if m == b"PSB4":
        return "psb4"
    if m == b"BMPA":
        return "bmpa"
    return "bin"


def _extract_mesh_obj(payload: bytes, kind: str, out_path: Path, name: str) -> bool:
    if kind == "psm2":
        mesh = psm2_mod.parse_psm2(payload)
        return _write_obj(str(out_path), name, mesh.positions, mesh.normals, mesh.primitives)
    if kind == "psc3":
        mesh = psc3_mod.parse_psc3(payload)
        return _write_obj(str(out_path), name, mesh.positions, mesh.normals, mesh.primitives)
    if kind == "psb4":
        mesh = psb4_mod.parse_psb4(payload)
        return _write_obj(str(out_path), name, mesh.positions, [], mesh.primitives)
    return False


def unpack_bundle(bundle_path: Path, out_dir: Path) -> Counter:
    """Unpack one bundle file into `out_dir` (created if missing)."""
    buf = bundle_path.read_bytes()
    out_dir.mkdir(parents=True, exist_ok=True)

    stats: Counter = Counter()
    manifest_lines: list[str] = [
        f"# bundle: {bundle_path.name}  ({len(buf):,} bytes)",
        f"# fields: offset  id          cat   rid    raw_size   kind   written",
    ]

    for idv, cat, rid, offset, payload in mcb_bundle.iter_records(buf):
        stats["records"] += 1
        cat_name = CATEGORY_NAMES.get(cat, f"cat{cat:04x}")

        # LZ-decode every payload.
        try:
            decoded = bytes(lz_decode(payload))
        except Exception as exc:  # noqa: BLE001
            stats["lz_fail"] += 1
            manifest_lines.append(
                f"  @0x{offset:07x}  {idv:#010x}  {cat_name:<4}  0x{rid:04x}  "
                f"LZ FAIL: {exc}"
            )
            continue

        kind = _classify_magic(decoded)
        base = f"{cat_name}_{rid:04x}"
        written = "-"

        try:
            if kind in ("psm2", "psc3", "psb4"):
                obj_path = out_dir / f"{base}.obj"
                ok = _extract_mesh_obj(decoded, kind, obj_path, base)
                stats[f"{cat_name}_{kind}"] += 1
                stats[f"{kind}_{'ok' if ok else 'empty'}"] += 1
                written = obj_path.name if ok else f"{base}.{kind} (empty)"
                # Also save the raw decoded payload next to the OBJ for
                # downstream tooling / re-parsing.
                (out_dir / f"{base}.{kind}").write_bytes(decoded)
            elif kind == "bmpa":
                png_path = out_dir / f"{base}.png"
                img = bmpa_mod.parse(decoded)
                bmpa_mod.write_png(img, png_path)
                stats[f"{cat_name}_bmpa"] += 1
                written = png_path.name
                (out_dir / f"{base}.bmpa").write_bytes(decoded)
            else:
                bin_path = out_dir / f"{base}.bin"
                bin_path.write_bytes(decoded)
                stats[f"{cat_name}_bin"] += 1
                written = bin_path.name
        except Exception as exc:  # noqa: BLE001
            stats[f"{kind}_fail"] += 1
            written = f"FAIL: {exc}"

        manifest_lines.append(
            f"  @0x{offset:07x}  {idv:#010x}  {cat_name:<4}  0x{rid:04x}  "
            f"{len(decoded):>9,}  {kind:<4}   {written}"
        )

    (out_dir / "_manifest.txt").write_text("\n".join(manifest_lines) + "\n")
    return stats


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Unpack MCB1 bundles to OBJ/PNG/BIN")
    ap.add_argument("--src", required=True, help="Bundle file OR directory of *.bin bundles")
    ap.add_argument("--dst", required=True, help="Output root directory")
    ap.add_argument("--limit", type=int, default=None, help="Only process first N bundles (dir mode)")
    args = ap.parse_args(argv)

    src = Path(args.src)
    dst = Path(args.dst)
    dst.mkdir(parents=True, exist_ok=True)

    bundles: list[Path]
    if src.is_file():
        bundles = [src]
    else:
        bundles = sorted(p for p in src.iterdir() if p.is_file() and p.suffix.lower() == ".bin")

    if args.limit is not None:
        bundles = bundles[: args.limit]

    grand = Counter()
    for b in bundles:
        sub = dst / b.stem if len(bundles) > 1 else dst
        stats = unpack_bundle(b, sub)
        grand.update(stats)

    print(f"Processed {len(bundles)} bundle(s). Totals:")
    for k in sorted(grand):
        print(f"  {k:<20} {grand[k]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
