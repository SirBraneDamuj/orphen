"""Check the static decode against a port `--scr-report`: every offset the port
executed an opcode at must be an instruction start here, with the same opcode.
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path

from .container import Disc, parse_scene_name
from .decode import Expr, Program


def instruction_starts(program: Program) -> dict[int, int]:
    """offset -> opcode for every statement, including ones inside expressions."""
    starts: dict[int, int] = {}

    def visit_expr(expr):
        if not isinstance(expr, Expr):
            return
        if expr.kind == "stmt":
            visit_instr(expr.instr)
        for child in expr.children:
            visit_expr(child)

    def visit_instr(instr):
        starts[instr.offset] = instr.code
        for _, value in instr.args:
            visit_expr(value)

    for instr in program.instrs.values():
        visit_instr(instr)
    for plist in program.point_lists.values():
        for point in plist.points:
            for value in point:
                visit_expr(value)
    return starts


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("scene")
    ap.add_argument("report", type=Path)
    ap.add_argument("--disc-root", default="disc")
    args = ap.parse_args(argv)
    program = Program(Disc(Path(args.disc_root)).script(*parse_scene_name(args.scene)))
    starts = instruction_starts(program)
    checked = bad = 0
    for line in args.report.read_text(errors="replace").splitlines():
        m = re.match(r"\s+0x([0-9a-f]+)\s+hits=\d+\s+first=0x([0-9a-f]+)", line)
        if not m:
            continue
        code, offset = int(m.group(1), 16), int(m.group(2), 16)
        checked += 1
        if starts.get(offset) != code:
            bad += 1
            got = starts.get(offset)
            print(f"opcode 0x{code:X} at 0x{offset:X}: static decode has "
                  f"{'nothing' if got is None else f'0x{got:X}'} there")
    print(f"{checked} executed opcode sites checked, {bad} disagree")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
