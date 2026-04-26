"""Tiny local HTTP server for browsing exported map glTFs.

Usage:
    python -m tools.map_viewer.server [--root out/map_gltf_all] [--port 8765]

Open the printed URL. Pick a scene from the dropdown, click the canvas to
capture the mouse, then use WASD + mouse to fly around. Esc releases.
Hold Shift to move faster, Q/E to descend/ascend.
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
        # Usually exactly one map_NNNN.gltf per scene.
        gltf = gltfs[0]
        scenes.append(
            {
                "scene": scene_dir.name,
                "file": gltf.name,
                "url": f"/maps/{scene_dir.name}/{gltf.name}",
            }
        )
    return scenes


def make_handler(root: Path, scenes: list[dict]):
    index_html = (HERE / "index.html").read_bytes()
    scenes_json = json.dumps(scenes).encode("utf-8")

    class Handler(http.server.SimpleHTTPRequestHandler):
        def do_GET(self):  # noqa: N802
            if self.path in ("/", "/index.html"):
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(index_html)))
                self.end_headers()
                self.wfile.write(index_html)
                return
            if self.path == "/scenes.json":
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(scenes_json)))
                self.end_headers()
                self.wfile.write(scenes_json)
                return
            if self.path.startswith("/maps/"):
                rel = self.path[len("/maps/") :].split("?", 1)[0]
                target = (root / rel).resolve()
                if root.resolve() not in target.parents and target != root.resolve():
                    self.send_error(403)
                    return
                if not target.is_file():
                    self.send_error(404)
                    return
                ctype = "application/octet-stream"
                if target.suffix == ".gltf":
                    ctype = "model/gltf+json"
                elif target.suffix == ".bin":
                    ctype = "application/octet-stream"
                elif target.suffix == ".png":
                    ctype = "image/png"
                data = target.read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", ctype)
                self.send_header("Content-Length", str(len(data)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(data)
                return
            self.send_error(404)

        def log_message(self, format, *args):  # quieter
            return

    return Handler


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="out/map_gltf_all", help="Directory of scene subfolders")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--no-open", action="store_true")
    args = ap.parse_args()

    root = Path(args.root).resolve()
    if not root.is_dir():
        print(f"[error] root not found: {root}")
        return 1

    scenes = build_scene_index(root)
    print(f"[viewer] serving {len(scenes)} scenes from {root}")
    handler = make_handler(root, scenes)
    with socketserver.TCPServer(("127.0.0.1", args.port), handler) as httpd:
        url = f"http://127.0.0.1:{args.port}/"
        print(f"[viewer] open {url}  (Ctrl+C to stop)")
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
