"""Flat, labelled listing of every routine -- the unstructured view."""
from __future__ import annotations

from .decode import Program
from .render import Namer, Renderer


def flat(program: Program, namer: Namer | None = None) -> list[str]:
    r = Renderer(program, namer)
    n = r.n
    lines = []
    for entry in sorted(program.routines):
        routine = program.routines[entry]
        sid = program.subproc_id(entry)
        lines.append("")
        lines.append(f"{program.routine_name(entry)}:   ; {', '.join(sorted(set(routine.names)))}"
                     + (f"  subproc id {sid}" if sid is not None and sid < 0x10000 else ""))
        for instr in routine.instrs:
            if instr.offset != entry and instr.offset in program.labels:
                lines.append(f"  {n.label(instr.offset)}:")
            text = statement(r, instr)
            lines.append(f"    {instr.offset:05X}  {text}")
        if routine.error:
            lines.append(f"    !! {routine.error}")
    return lines


def statement(r: Renderer, instr) -> str:
    n = r.n
    code = instr.code
    if code == 0x04:
        return "end"
    if code in (0x00, 0x05):
        return "nop"
    if code == 0x0A:
        return "debug_code()"
    if code in (0x06, 0x08):
        return f"skip4  ; 0x{instr.extra['skipped']:08X}"
    if code in (0x03, 0x07, 0x09):
        return f"goto {n.label(instr.targets[0][1])}" + (f"  ; op {code:02X}" if code != 0x03 else "")
    if code == 0x01:
        return f"if !({r.expr(instr.args[0][1])}) goto {n.label(instr.targets[0][1])}"
    if code == 0x02:
        cases = ", ".join(f"{_case(k)}: {n.label(t)}" for k, t in instr.extra["cases"])
        return f"switch ({r.expr(instr.args[0][1])}) {{ {cases}; default: {n.label(instr.extra['default'])} }}"
    return r.call(instr)


def _case(key: int) -> str:
    return str(key - (1 << 32) if key & 0x80000000 else key)
