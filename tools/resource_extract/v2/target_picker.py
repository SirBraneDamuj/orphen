#!/usr/bin/env python3
"""Phase 1: pick a small, textured PSC3 to use as a Blender validation
target before tackling Orphen.

Operates directly on MCB0.BIN + MCB1.BIN with no big disk extraction:
walks every bundle in memory, parses each PSC3 header, and ranks
candidates by a simplicity score. The lower the score, the easier the
asset is to debug visually:

    score = mesh_bytes/1000 + submeshes*5 + subdraws*1.5 + materials*2

Only PSC3s with `header[0x40] != 0` are considered (i.e., the renderer's
texture-binding path is engaged for them). For each candidate we report
which BMPA texture rids live in the same bundle, since those are the
universe of possible `map_Kd` matches.

Usage:

    python -m tools.resource_extract.v2.target_picker [--top 20]
        [--mcb0 MCB0.BIN] [--mcb1 MCB1.BIN]

Output is plain text to stdout.
"""
from __future__ import annotations

import argparse
import os
import struct
import sys
from collections import Counter
from typing import Iterator, List, Tuple

from ..baseline.lz_decoder import decode_bytes as lz_decode
from . import mcb as mcb_mod
from . import mcb_bundle


def _u16(b: bytes, o: int) -> int:
    return struct.unpack_from("<H", b, o)[0]


def _s16(b: bytes, o: int) -> int:
    return struct.unpack_from("<h", b, o)[0]


def _u32(b: bytes, o: int) -> int:
    return struct.unpack_from("<I", b, o)[0]


def quick_psc3_summary(buf: bytes) -> dict | None:
    """Return a minimal header summary for a PSC3 blob, or None."""
    if len(buf) < 0x50 or _u32(buf, 0) != 0x33435350:  # "PSC3"
        return None
    submesh_count = _s16(buf, 0x04)
    tex_tbl = _u32(buf, 0x40)
    offs_subdraws = _u32(buf, 0x24)  # called "offs_materials" elsewhere
    # Subdraw count: header doesn't store it directly; estimate from the
    # gap between offs_materials and offs_normals (each subdraw is 10 bytes).
    offs_normals = _u32(buf, 0x28)
    if offs_subdraws and offs_normals and offs_normals > offs_subdraws:
        sd_count = (offs_normals - offs_subdraws) // 10
    else:
        sd_count = -1
    # Distinct tex_flags across subdraws gives a rough materials lower bound.
    distinct_flags: set[int] = set()
    enabled_flags: set[int] = set()
    if sd_count > 0:
        for i in range(min(sd_count, 256)):
            o = offs_subdraws + i * 10
            if o + 10 > len(buf):
                break
            f = _u16(buf, o + 8)
            distinct_flags.add(f)
            enable = (f >> 14) & 0x3
            slot = f & 0x7f
            if enable != 0 and slot != 0x7f:
                enabled_flags.add(f)
    return {
        "size": len(buf),
        "submeshes": max(0, submesh_count),
        "subdraws": max(0, sd_count),
        "distinct_flags": len(distinct_flags),
        "textured_flags": len(enabled_flags),
        "tex_tbl_off": tex_tbl,
    }


def simplicity_score(s: dict) -> float:
    return (s["size"] / 1000.0) + s["submeshes"] * 5 + s["subdraws"] * 1.5 + s["distinct_flags"] * 2


def _mcb1_iter_bundles(mcb0_path: str, mcb1_path: str) -> Iterator[Tuple[str, bytes]]:
    """Yield (bundle_name, bundle_bytes) for every non-empty section."""
    sections = mcb_mod.load_mcb0(mcb0_path)
    with open(mcb1_path, "rb") as fh:
        for s_idx, section in enumerate(sections):
            for e_idx, (off, size) in enumerate(section):
                if size == 0:
                    continue
                fh.seek(off)
                yield f"s{s_idx:02d}_e{e_idx:03d}", fh.read(size)


def scan(mcb0_path: str, mcb1_path: str) -> List[dict]:
    candidates: List[dict] = []
    for bname, bbuf in _mcb1_iter_bundles(mcb0_path, mcb1_path):
        # First pass: collect BMPA rids in this bundle (texture pool).
        tex_rids: List[int] = []
        psc3_records: List[Tuple[int, bytes]] = []
        for idv, cat, rid, _off, payload in mcb_bundle.iter_records(bbuf):
            try:
                decoded = lz_decode(payload)
            except Exception:
                continue
            if not decoded or len(decoded) < 4:
                continue
            magic = decoded[:4]
            if magic == b"BMPA":
                tex_rids.append(rid)
            elif magic == b"PSC3":
                psc3_records.append((rid, decoded))
        # Second pass: summarize PSC3s.
        for rid, blob in psc3_records:
            s = quick_psc3_summary(blob)
            if not s:
                continue
            if s["tex_tbl_off"] == 0:
                continue  # not a texture-binding mesh
            if s["textured_flags"] == 0:
                continue  # all subdraws untextured (vertex-color only)
            candidates.append(
                {
                    "bundle": bname,
                    "rid": rid,
                    **s,
                    "tex_rids": tex_rids[:],
                    "score": simplicity_score(s),
                }
            )
    candidates.sort(key=lambda c: c["score"])
    return candidates


def main(argv: List[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Pick a Phase 1 PSC3 validation target")
    ap.add_argument("--mcb0", default="MCB0.BIN")
    ap.add_argument("--mcb1", default="MCB1.BIN")
    ap.add_argument("--top", type=int, default=20)
    args = ap.parse_args(argv)

    if not os.path.isfile(args.mcb0) or not os.path.isfile(args.mcb1):
        print(f"missing {args.mcb0} or {args.mcb1}", file=sys.stderr)
        return 2

    print(f"Scanning {args.mcb1} via {args.mcb0}...", file=sys.stderr)
    candidates = scan(args.mcb0, args.mcb1)
    print(f"Found {len(candidates)} textured PSC3 candidates.\n")
    print(f"{'#':>3}  {'bundle':<10}  {'rid':>6}  {'size':>6}  "
          f"{'sm':>3}  {'sd':>3}  {'mat':>3}  {'tex':>3}  {'score':>7}  texs")
    for i, c in enumerate(candidates[: args.top]):
        tex_str = ",".join(f"{r:04x}" for r in c["tex_rids"][:6])
        if len(c["tex_rids"]) > 6:
            tex_str += f",...({len(c['tex_rids'])})"
        print(f"{i + 1:>3}  {c['bundle']:<10}  "
              f"{c['rid']:>04x}    "
              f"{c['size']:>6}  "
              f"{c['submeshes']:>3}  "
              f"{c['subdraws']:>3}  "
              f"{c['distinct_flags']:>3}  "
              f"{c['textured_flags']:>3}  "
              f"{c['score']:>7.1f}  {tex_str}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
