"""Deploy the viewer to an S3 bucket as a fully static site.

What it produces in the bucket:

    /index.html               (map viewer)
    /models/index.html        (model viewer)
    /scenes.json              (built here from out/map_gltf_all)
    /models-index.json        (copy of out/models/_index.json)
    /maps/<scene>/<file>      (mirrors out/map_gltf_all)
    /models/<grp>/aid<N>/...  (mirrors out/models)

The HTML pages already fetch these absolute paths, so no JS changes are
required. The only requirement on the front-end is that ``/models/``
resolves to ``/models/index.html``; this works automatically with
S3 Static Website hosting, or with a CloudFront Function rewriting
``/(.*)/$`` -> ``/$1/index.html``.

Requirements: ``aws`` CLI on PATH with credentials configured.

Usage:
    python -m tools.map_viewer.deploy_aws \
        --bucket my-orphen-viewer \
        [--cf-distribution E2ABC...]   # optional CloudFront invalidation
        [--dry-run]
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

# Long-cache headers for content-addressed-ish assets (rebuild = republish).
LONG_CACHE = "public, max-age=31536000, immutable"
SHORT_CACHE = "public, max-age=60"


def _run(cmd: list[str], dry_run: bool) -> None:
    print("$ " + " ".join(cmd))
    if dry_run:
        return
    subprocess.run(cmd, check=True)


def build_scenes_json(map_root: Path, out_path: Path) -> int:
    scenes = []
    for scene_dir in sorted(p for p in map_root.iterdir() if p.is_dir()):
        gltfs = sorted(scene_dir.glob("*.gltf"))
        if not gltfs:
            continue
        gltf = gltfs[0]
        scenes.append({
            "scene": scene_dir.name,
            "file": gltf.name,
            "url": f"/maps/{scene_dir.name}/{gltf.name}",
        })
    out_path.write_text(json.dumps(scenes))
    return len(scenes)


def upload_file(src: Path, bucket: str, key: str, ctype: str,
                cache: str, dry_run: bool) -> None:
    _run([
        "aws", "s3", "cp", str(src), f"s3://{bucket}/{key}",
        "--content-type", ctype,
        "--cache-control", cache,
    ], dry_run)


def sync_tree(src: Path, bucket: str, prefix: str, dry_run: bool,
              extra_excludes: list[str] | None = None) -> None:
    cmd = [
        "aws", "s3", "sync", str(src), f"s3://{bucket}/{prefix}",
        "--cache-control", LONG_CACHE,
        "--delete",
    ]
    for pat in (extra_excludes or []):
        cmd.extend(["--exclude", pat])
    _run(cmd, dry_run)


def fixup_gltf_content_type(bucket: str, prefix: str, dry_run: bool) -> None:
    """Re-stamp every .gltf under prefix with model/gltf+json.

    aws s3 sync uses mimetypes.guess_type which doesn't know .gltf, so the
    initial sync stamps them as application/octet-stream (or omits it). The
    files still load (three.js GLTFLoader doesn't care about Content-Type),
    but a correct header is nicer for caches and curl. This pass copies each
    object to itself with the right Content-Type set.
    """
    cmd = [
        "aws", "s3", "cp", f"s3://{bucket}/{prefix}", f"s3://{bucket}/{prefix}",
        "--recursive",
        "--exclude", "*",
        "--include", "*.gltf",
        "--content-type", "model/gltf+json",
        "--cache-control", LONG_CACHE,
        "--metadata-directive", "REPLACE",
    ]
    _run(cmd, dry_run)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bucket", required=True, help="Target S3 bucket name")
    ap.add_argument("--map-root", default="out/map_gltf_all")
    ap.add_argument("--models-root", default="out/models")
    ap.add_argument("--cf-distribution", default=None,
                    help="CloudFront distribution ID for cache invalidation")
    ap.add_argument("--dry-run", action="store_true",
                    help="Print commands but don't execute them")
    ap.add_argument("--skip-fixup", action="store_true",
                    help="Skip the .gltf Content-Type fix-up pass")
    args = ap.parse_args(argv)

    map_root = (REPO_ROOT / args.map_root).resolve()
    models_root = (REPO_ROOT / args.models_root).resolve()
    if not map_root.is_dir():
        print(f"[error] map root not found: {map_root}", file=sys.stderr)
        return 1
    if not models_root.is_dir():
        print(f"[error] models root not found: {models_root}", file=sys.stderr)
        return 1

    # Stage scenes.json (the only file we need to compute on the fly).
    stage = REPO_ROOT / "out" / "aws_stage"
    stage.mkdir(parents=True, exist_ok=True)
    scenes_path = stage / "scenes.json"
    n_scenes = build_scenes_json(map_root, scenes_path)
    print(f"[stage] scenes.json: {n_scenes} scenes")

    # 1. JSON indexes (small, change every rebuild).
    upload_file(scenes_path, args.bucket, "scenes.json",
                "application/json", SHORT_CACHE, args.dry_run)
    upload_file(models_root / "_index.json", args.bucket, "models-index.json",
                "application/json", SHORT_CACHE, args.dry_run)

    # 2. Asset trees (uses --delete; do these before placing HTML pages
    #    that live inside the synced prefixes).
    sync_tree(map_root, args.bucket, "maps/", args.dry_run)
    # Exclude _index.json from the models sync because we put a renamed copy
    # at the bucket root above; don't waste an object on the duplicate.
    sync_tree(models_root, args.bucket, "models/", args.dry_run,
              extra_excludes=["_index.json"])

    # 3. HTML pages (placed last so the models/ sync's --delete doesn't
    #    remove the model viewer page).
    upload_file(HERE / "index.html", args.bucket, "index.html",
                "text/html; charset=utf-8", SHORT_CACHE, args.dry_run)
    upload_file(HERE / "index_anim.html", args.bucket, "models/index.html",
                "text/html; charset=utf-8", SHORT_CACHE, args.dry_run)

    # 4. Repair .gltf Content-Type.
    if not args.skip_fixup:
        fixup_gltf_content_type(args.bucket, "maps/", args.dry_run)
        fixup_gltf_content_type(args.bucket, "models/", args.dry_run)

    # 5. Optional CloudFront invalidation.
    if args.cf_distribution:
        _run([
            "aws", "cloudfront", "create-invalidation",
            "--distribution-id", args.cf_distribution,
            "--paths", "/*",
        ], args.dry_run)

    print("[done] deploy complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
