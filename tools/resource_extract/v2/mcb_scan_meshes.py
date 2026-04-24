#!/usr/bin/env python3
"""Scan MCB bundles for embedded mesh assets and export every valid one.

MCB1 sections are "scene bundles" — a header + script bytecode + a TOC-indexed
set of sub-assets of varying type (PSM2, PSC3, PSB4). We don't yet understand
the TOC layout, so we scan for magics directly and validate each candidate by
attempting to parse it with the existing v2 parsers. Real assets produce
valid geometry; false-positive magic matches inside vertex data fail the
parse (or yield absurd counts) and are filtered out.

Usage:
    python -m tools.resource_extract.v2.mcb_scan_meshes \
        --src out/mcb --dst out/mcb_meshes
"""
from __future__ import annotations

import argparse
import os
import struct
from collections import Counter
from typing import List

from tools.resource_extract.v2 import psm2 as psm2_mod
from tools.resource_extract.v2 import psc3 as psc3_mod
from tools.resource_extract.v2 import psb4 as psb4_mod

MAGIC_MAP = {
    b'PSM2': ('PSM2', psm2_mod),
    b'PSC3': ('PSC3', psc3_mod),
    b'PSB4': ('PSB4', psb4_mod),
}


def _find_all(buf: bytes, needle: bytes) -> List[int]:
    out: List[int] = []
    pos = 0
    while True:
        i = buf.find(needle, pos)
        if i < 0:
            break
        out.append(i)
        pos = i + 1
    return out


def _is_plausible_psc3(buf: bytes, off: int) -> bool:
    if off + 0x30 > len(buf):
        return False
    submesh_count = struct.unpack_from('<h', buf, off + 4)[0]
    if submesh_count <= 0 or submesh_count > 256:
        return False
    # Section offsets must fall within a reasonable range (file-relative from
    # the chunk start). If any exceeds a large-but-sane bound it's garbage.
    for hdr_off in (0x08, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28):
        v = struct.unpack_from('<I', buf, off + hdr_off)[0]
        if v > 0x10000000:
            return False
    return True


def _is_plausible_psm2(buf: bytes, off: int) -> bool:
    # PSM2 has its own sanity shape; let the parser decide by attempting parse.
    return off + 0x20 <= len(buf)


def _is_plausible_psb4(buf: bytes, off: int) -> bool:
    if off + 0x10 > len(buf):
        return False
    for hdr_off in (4, 8, 0x0c):
        v = struct.unpack_from('<I', buf, off + hdr_off)[0]
        if v > 0x10000000:
            return False
    return True


def _try_parse(mod, slice_: bytes):
    """Attempt to parse; return a mesh object or None on failure / empty."""
    try:
        if hasattr(mod, 'parse_psc3'):
            m = mod.parse_psc3(slice_)
        elif hasattr(mod, 'parse_psm2'):
            m = mod.parse_psm2(slice_)
        elif hasattr(mod, 'parse_psb4'):
            m = mod.parse_psb4(slice_)
        else:
            return None
        if m.positions and m.primitives:
            return m
    except Exception:
        return None
    return None


def _write_obj(mod, mesh, out_path: str, name: str) -> None:
    # psm2 and psc3 have slightly different OBJ writer signatures; call each
    # module's writer appropriately. All three return (nverts, nfaces).
    mod.write_obj(mesh, out_path, name=name)


def process_bundle(bundle_path: str, dst_dir: str,
                   tag: str) -> Counter:
    buf = open(bundle_path, 'rb').read()
    stats: Counter = Counter()
    # Gather all candidate (offset, magic) tuples
    hits = []
    for needle, (name, _mod) in MAGIC_MAP.items():
        for i in _find_all(buf, needle):
            hits.append((i, name))
    hits.sort()

    for idx, (off, name) in enumerate(hits):
        # Slice from this magic to the next magic (or EOF)
        next_off = hits[idx + 1][0] if idx + 1 < len(hits) else len(buf)
        slice_ = buf[off:next_off]
        # Plausibility filter
        if name == 'PSC3':
            if not _is_plausible_psc3(buf, off):
                stats[f'{name}_rej'] += 1
                continue
            mod = psc3_mod
        elif name == 'PSM2':
            if not _is_plausible_psm2(buf, off):
                stats[f'{name}_rej'] += 1
                continue
            mod = psm2_mod
        elif name == 'PSB4':
            if not _is_plausible_psb4(buf, off):
                stats[f'{name}_rej'] += 1
                continue
            mod = psb4_mod
        else:
            continue

        mesh = _try_parse(mod, slice_)
        if mesh is None:
            stats[f'{name}_fail'] += 1
            continue

        os.makedirs(dst_dir, exist_ok=True)
        out_name = f"{tag}_{idx:03d}_{name}.obj"
        out_path = os.path.join(dst_dir, out_name)
        try:
            _write_obj(mod, mesh, out_path, name=out_name[:-4])
            stats[f'{name}_ok'] += 1
        except Exception:
            stats[f'{name}_fail'] += 1
    return stats


def main(argv: List[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Scan MCB bundles for embedded mesh assets"
    )
    ap.add_argument('--src', default='out/mcb',
                    help='directory of MCB bundles (from mcb.py)')
    ap.add_argument('--dst', default='out/mcb_meshes',
                    help='output directory for OBJs')
    ap.add_argument('--limit', type=int, default=0,
                    help='process at most N bundles (0 = all)')
    args = ap.parse_args(argv)

    files = sorted(os.listdir(args.src))
    if args.limit:
        files = files[:args.limit]

    totals: Counter = Counter()
    for fn in files:
        tag = os.path.splitext(fn)[0]  # e.g. s01_e011
        path = os.path.join(args.src, fn)
        st = process_bundle(path, args.dst, tag)
        totals.update(st)

    print(f"Processed {len(files)} bundle(s)")
    print("\nResults:")
    for k in sorted(totals):
        print(f"  {k:>12}: {totals[k]:>6}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
