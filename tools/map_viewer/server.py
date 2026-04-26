"""Tiny local HTTP server for browsing exported map glTFs and models.

Usage:
    python -m tools.map_viewer.server [--root out/map_gltf_all] [--models-root out/models] [--port 8765]

Open the printed URL. `/` is the map viewer; `/animations` is the
deduplicated model viewer (Model -> Anim id, with the manifest of
scenes referencing each model shown alongside).
"""
from __future__ import annotations

import argparse
import http.server
import json
import socketserver
import webbrowser
from pathlib import Path


HERE = Path(__file__).resolve().parent


def build_scene_index(root: Path) -> list[dict]:
    scenes: list[dict] = []
    for scene_dir in sorted(p for p in root.iterdir() if p.is_dir()):
        gltfs = sorted(scene_dir.glob("*.gltf"))
        if not gltfs:
            continue
        gltf = gltfs[0]
        scenes.append(
            {
                "scene": scene_dir.name,
                "file": gltf.name,
                "url": f"/maps/{scene_dir.name}/{gltf.name}",
            }
        )
    return scenes


def build_models_index(root: Path) -> dict:
    """Read out/models/_index.json (produced by psc3_export_all)."""
    if not root.is_dir():
        return {}
    idx_path = root / "_index.json"
    if idx_path.is_file():
        try:
            return json.loads(idx_path.read_text())
        except Exception:
            pass
    # Fall back: scan directory tree.
    out: dict = {}
    for model_dir in sorted(p for p in root.iterdir() if p.is_dir()):
        manifest = model_dir / "_scenes.json"
        meta = {}
        if manifest.is_file():
            try:
                meta = json.loads(manifest.read_text())
            except Exception:
                meta = {}
        aid_dirs = sorted(p for p in model_dir.iterdir() if p.is_dir() and p.name.startswith("aid"))
        meta["aid_count"] = len(aid_dirs)
        meta.setdefault("name", model_dir.name)
        out[model_dir.name] = meta
    return out


def _safe_under(base: Path, rel: str) -> Path | None:
    target = (base / rel).resolve()
    base_r = base.resolve()
    try:
        target.relative_to(base_r)
    except ValueError:
        return None
    return target


def _ctype_for(path: Path) -> str:
    if path.suffix == ".gltf":
        return "model/gltf+json"
    if path.suffix == ".bin":
        return "application/octet-stream"
    if path.suffix == ".png":
        return "image/png"
    if path.suffix in (".jpg", ".jpeg"):
        return "image/jpeg"
    return "application/octet-stream"


def make_handler(map_root: Path, models_root: Path, scenes: list[dict], models_index: dict):
    index_html = (HERE / "index.html").read_bytes()
    anim_html = (HERE / "index_anim.html").read_bytes()
    scenes_json = json.dumps(scenes).encode("utf-8")
    models_json = json.dumps(models_index).encode("utf-8")

    class Handler(http.server.SimpleHTTPRequestHandler):
        def _send_bytes(self, data: bytes, ctype: str) -> None:
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def _serve_file(self, base: Path, rel: str) -> None:
            target = _safe_under(base, rel)
            if target is None or not target.is_file():
                self.send_error(404)
                return
            self._send_bytes(target.read_bytes(), _ctype_for(target))

        def do_GET(self):  # noqa: N802
            path = self.path.split("?", 1)[0]
            if path in ("/", "/index.html"):
                self._send_bytes(index_html, "text/html; charset=utf-8")
                return
            if path in ("/animations", "/animations/", "/models", "/models/", "/index_anim.html"):
                self._send_bytes(anim_html, "text/html; charset=utf-8")
                return
            if path == "/scenes.json":
                self._send_bytes(scenes_json, "application/json")
                return
            if path == "/models-index.json":
                self._send_bytes(models_json, "application/json")
                return
            if path.startswith("/maps/"):
                self._serve_file(map_root, path[len("/maps/"):])
                return
            if path.startswith("/models/"):
                self._serve_file(models_root, path[len("/models/"):])
                return
            self.send_error(404)

        def log_message(self, format, *args):  # quieter
            return

    return Handler


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="out/map_gltf_all", help="Directory of map scene subfolders")
    ap.add_argument("--models-root", default="out/models", help="Directory of deduplicated model folders")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--no-open", action="store_true")
    args = ap.parse_args()

    map_root = Path(args.root).resolve()
    models_root = Path(args.models_root).resolve()
    if not map_root.is_dir():
        print(f"[error] map root not found: {map_root}")
        return 1

    scenes = build_scene_index(map_root)
    models_index = build_models_index(models_root)
    print(f"[viewer] maps: {len(scenes)} scenes from {map_root}")
    print(f"[viewer] models: {len(models_index)} unique models from {models_root}")
    handler = make_handler(map_root, models_root, scenes, models_index)
    with socketserver.TCPServer(("127.0.0.1", args.port), handler) as httpd:
        url = f"http://127.0.0.1:{args.port}/"
        print(f"[viewer] open {url}  (Ctrl+C to stop)")
        print(f"[viewer]      {url}models")
        if not args.no_open:
            try:
                webbrowser.open(url)
            except Exception:
                pass
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n[viewer] bye")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
