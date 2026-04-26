#!/usr/bin/env python3
"""Deduplicated PSC3 -> glTF export across every unpacked scene.

Walks ``--src`` (default ``out/target_all``) for ``<scene>/grp_*.psc3``
files. Identical model bytes that appear under the same basename in
multiple scenes are exported only once. The output layout is

    <dst>/<grp_name>/aid<N>/<grp_name>.gltf
                    /aid<N>/<grp_name>.bin
                    /aid<N>/tex_*.png
                    /_scenes.json   (manifest of scenes that use this model)
    <dst>/_index.json     (top-level: model -> stats)

In the rare case where the same basename has different bytes in
different scenes, each variant is emitted under
``<grp_name>__<hash6>/`` and recorded separately.

CLI:
    python -m tools.resource_extract.v2.psc3_export_all \
        --src out/target_all --dst out/models
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
import time
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Tuple

from .psc3_anim_decode import parse_anim_table
from .psc3_full import MAGIC_PSC3, parse_psc3_full, _u32
from .psc3_gltf_anim import emit_animated


def _sha6(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()[:6]


def _gather(src: Path) -> Dict[Tuple[str, str], Dict]:
    """Group PSC3 files by (basename, sha6).

    Returns mapping (name, sha) -> { 'canonical': Path, 'scenes': [...], 'name': str, 'sha': str }.
    """
    groups: Dict[Tuple[str, str], Dict] = {}
    scene_dirs = sorted(p for p in src.iterdir() if p.is_dir())
    for scene_dir in scene_dirs:
        for psc3 in sorted(scene_dir.glob("grp_*.psc3")):
            data = psc3.read_bytes()
            if len(data) < 4 or _u32(data, 0) != MAGIC_PSC3:
                continue
            name = psc3.stem
            sha = _sha6(data)
            key = (name, sha)
            entry = groups.get(key)
            if entry is None:
                groups[key] = {
                    "name": name,
                    "sha": sha,
                    "canonical": psc3,
                    "scenes": [scene_dir.name],
                }
            else:
                entry["scenes"].append(scene_dir.name)
    return groups


def _model_dirname(name: str, sha: str, has_variants: bool) -> str:
    return f"{name}__{sha}" if has_variants else name


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="out/target_all", help="Unpacked scene root")
    ap.add_argument("--dst", default="out/models", help="Deduped model output root")
    ap.add_argument("--limit", type=int, default=0, help="Optional: stop after N models (debug)")
    ap.add_argument("--skip-existing", action="store_true",
                    help="Skip a model if its destination dir already has a .gltf")
    args = ap.parse_args()

    src = Path(args.src).resolve()
    dst = Path(args.dst).resolve()
    if not src.is_dir():
        print(f"[error] src not found: {src}", file=sys.stderr)
        return 1
    dst.mkdir(parents=True, exist_ok=True)

    print(f"[scan] gathering PSC3 files under {src}")
    t0 = time.time()
    groups = _gather(src)

    # Track basenames that have multiple distinct hashes -> need suffix.
    by_name: Dict[str, List[Tuple[str, str]]] = defaultdict(list)
    for (name, sha) in groups.keys():
        by_name[name].append((name, sha))
    multi_variant = {n for n, lst in by_name.items() if len(lst) > 1}

    n_files = sum(len(g["scenes"]) for g in groups.values())
    print(f"[scan] {n_files} PSC3 files -> {len(groups)} unique models "
          f"({len(multi_variant)} basenames with multiple variants) in {time.time()-t0:.1f}s")

    index: Dict[str, Dict] = {}
    processed = 0
    for (name, sha), entry in sorted(groups.items()):
        has_variants = name in multi_variant
        out_name = _model_dirname(name, sha, has_variants)
        out_dir = dst / out_name
        canonical: Path = entry["canonical"]
        scenes: List[str] = sorted(set(entry["scenes"]))

        # Always (re)write the manifest so it stays in sync.
        out_dir.mkdir(parents=True, exist_ok=True)
        manifest = {
            "name": name,
            "sha6": sha,
            "canonical_source": str(canonical.relative_to(src.parent.parent)) if src.parent.parent in canonical.parents else str(canonical),
            "scenes": scenes,
            "scene_count": len(scenes),
        }
        (out_dir / "_scenes.json").write_text(json.dumps(manifest, indent=2))

        existing_gltfs = list(out_dir.glob("aid*/*.gltf"))
        if args.skip_existing and existing_gltfs:
            index[out_name] = {
                **manifest,
                "aid_count": len({p.parent.name for p in existing_gltfs}),
                "skipped": True,
            }
            continue

        # Parse + emit.
        try:
            data = canonical.read_bytes()
            mesh = parse_psc3_full(data)
            h = mesh.header
            if not h["offs_u0c"]:
                index[out_name] = {**manifest, "aid_count": 0, "note": "no anim table"}
                continue
            anim_total = len(parse_anim_table(data, h["offs_u0c"]))
            bundle_dir = str(canonical.parent)

            for aid in range(anim_total):
                aid_dir = out_dir / f"aid{aid}"
                gltf_path = aid_dir / f"{name}.gltf"
                emit_animated(
                    data, mesh, str(gltf_path), name,
                    anim_ids=[aid],
                    bundle_dir=bundle_dir,
                    png_override=None,
                )
            index[out_name] = {
                **manifest,
                "aid_count": anim_total,
            }
            processed += 1
            if processed % 25 == 0:
                print(f"[emit] {processed} models written...")
            if args.limit and processed >= args.limit:
                print(f"[stop] hit --limit {args.limit}")
                break
        except Exception as exc:  # noqa: BLE001
            print(f"[warn] {name} ({sha}): {exc}", file=sys.stderr)
            index[out_name] = {**manifest, "error": str(exc)}

    (dst / "_index.json").write_text(json.dumps(index, indent=2))
    print(f"[done] {processed}/{len(groups)} unique models emitted into {dst}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
