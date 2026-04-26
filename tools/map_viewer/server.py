"""Tiny local HTTP server for browsing exported map glTFs and animations.

Usage:
    python -m tools.map_viewer.server [--root out/map_gltf_all] [--anim-root out/anim] [--port 8765]

Open the printed URL. `/` is the map viewer; `/animations` is the
model+animation viewer (scene → model → anim id).
"""
from __future__ import annotations

import argparse
import http.server
import json
import re
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


_AID_RE = re.compile(r"^aid(\d+)$")


def build_anim_index(root: Path) -> dict:
    """Return {scene: {model: [{aid, url}]}} sorted naturally."""
    tree: dict = {}
    if not root.is_dir():
        return tree
    for scene_dir in sorted(p for p in root.iterdir() if p.is_dir()):
        models: dict = {}
        for model_dir in sorted(p for p in scene_dir.iterdir() if p.is_dir()):
            anims: list[dict] = []
            aid_dirs = []
            for aid_dir in model_dir.iterdir():
                if not aid_dir.is_dir():
                    continue
                m = _AID_RE.match(aid_dir.name)
                if not m:
                    continue
                aid_dirs.append((int(m.group(1)), aid_dir))
            aid_dirs.sort(key=lambda t: t[0])
            for aid_num, aid_dir in aid_dirs:
                gltfs = sorted(aid_dir.glob("*.gltf"))
                if not gltfs:
                    continue
                gltf = gltfs[0]
                rel = gltf.relative_to(root).as_posix()
                anims.append(
                    {
                        "aid": aid_dir.name,
                        "file": gltf.name,
                        "url": f"/anim/{rel}",
                    }
                )
            if anims:
                models[model_dir.name] = anims
        if models:
            tree[scene_dir.name] = models
    return tree


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
    if path.suffix == ".jpg" or path.suffix == ".jpeg":
        return "image/jpeg"
    return "application/octet-stream"


def make_handler(map_root: Path, anim_root: Path, scenes: list[dict], anim_tree: dict):
    index_html = (HERE / "index.html").read_bytes()
    anim_html = (HERE / "index_anim.html").read_bytes()
    scenes_json = json.dumps(scenes).encode("utf-8")
    anim_json = json.dumps(anim_tree).encode("utf-8")

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
            if path in ("/animations", "/animations/", "/index_anim.html"):
                self._send_bytes(anim_html, "text/html; charset=utf-8")
                return
            if path == "/scenes.json":
                self._send_bytes(scenes_json, "application/json")
                return
            if path == "/anim-index.json":
                self._send_bytes(anim_json, "application/json")
                return
            if path.startswith("/maps/"):
                self._serve_file(map_root, path[len("/maps/"):])
                return
            if path.startswith("/anim/"):
                self._serve_file(anim_root, path[len("/anim/"):])
                return
            self.send_error(404)

        def log_message(self, format, *args):  # quieter
            return

    return Handler


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="out/map_gltf_all", help="Directory of map scene subfolders")
    ap.add_argument("--anim-root", default="out/anim", help="Directory of animation scene subfolders")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--no-open", action="store_true")
    args = ap.parse_args()

    map_root = Path(args.root).resolve()
    anim_root = Path(args.anim_root).resolve()
    if not map_root.is_dir():
        print(f"[error] map root not found: {map_root}")
        return 1

    scenes = build_scene_index(map_root)
    anim_tree = build_anim_index(anim_root)
    n_models = sum(len(m) for m in anim_tree.values())
    n_anims = sum(len(a) for m in anim_tree.values() for a in m.values())
    print(f"[viewer] maps: {len(scenes)} scenes from {map_root}")
    print(f"[viewer] anims: {len(anim_tree)} scenes / {n_models} models / {n_anims} clips from {anim_root}")
    handler = make_handler(map_root, anim_root, scenes, anim_tree)
    with socketserver.TCPServer(("127.0.0.1", args.port), handler) as httpd:
        url = f"http://127.0.0.1:{args.port}/"
        print(f"[viewer] open {url}  (Ctrl+C to stop)")
        print(f"[viewer]      {url}animations")
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
