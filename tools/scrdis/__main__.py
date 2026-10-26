"""python -m tools.scrdis <scene> [-o out.html] [--text]

Disassemble a scene script and write an HTML explorer (default) or plain
structured pseudocode (--text).
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .container import Disc, parse_scene_name
from .decode import Program
from .explorer import Explorer
from .pseudo import text


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="python -m tools.scrdis", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scene", help="sNN_eMMM, or a path to a decoded script blob")
    ap.add_argument("-o", "--out", type=Path, help="output file (default out/scrdis/<scene>.html)")
    ap.add_argument("--text", action="store_true", help="write structured pseudocode instead of HTML")
    ap.add_argument("--artifact", action="store_true",
                    help="omit the doctype/head/body wrapper, which an Artifact publish adds itself")
    ap.add_argument("--disc-root", default="disc")
    args = ap.parse_args(argv)

    scene = None
    if Path(args.scene).is_file():
        data = Path(args.scene).read_bytes()
        name = Path(args.scene).stem
    else:
        scene = parse_scene_name(args.scene)
        data = Disc(Path(args.disc_root)).script(*scene)
        name = args.scene
        if data is None:
            print(f"{args.scene} has no script record", file=sys.stderr)
            return 1
    program = Program(data)

    if args.text:
        output = text(program)
        if args.out is None:
            sys.stdout.write(output + "\n")
            return 0
    else:
        output = Explorer(program, f"{name} scene script", scene).page(fragment=args.artifact)
    out = args.out or Path("out/scrdis") / f"{name}.html"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(output, encoding="utf-8")
    print(f"{name}: {len(program.routines)} routines, {len(program.instrs)} instructions, "
          f"{len(program.streams)} streams, {len(program.conflicts)} decode errors -> {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
