"""Structured pseudocode for one routine."""
from __future__ import annotations

from .decode import Program
from .listing import _case, statement
from .render import Namer, Renderer
from .structure import Node, structure

INDENT = "    "


class Pseudo:
    def __init__(self, program: Program, namer: Namer | None = None):
        self.program = program
        self.r = Renderer(program, namer)
        self.n = self.r.n

    def routine(self, entry: int, routine=None) -> tuple[list[tuple[int | None, int, str]], bool]:
        """Lines as (offset or None, depth, text), and whether it structured."""
        routine = routine or self.program.routines[entry]
        labels = routine_labels(self.program, routine)
        nodes = structure(self.program, routine.instrs, labels)
        if nodes is None:
            lines = []
            for instr in routine.instrs:
                if instr.offset != entry and instr.offset in labels:
                    lines.append((None, 0, f"{self.n.label(instr.offset)}:"))
                lines.append((instr.offset, 1, statement(self.r, instr)))
            return lines, False
        lines: list[tuple[int | None, int, str]] = []
        self._nodes(nodes, 1, lines, top=True)
        return lines, True

    def _nodes(self, nodes: list[Node], depth: int, out, top: bool = False):
        for k, node in enumerate(nodes):
            last = top and k == len(nodes) - 1
            self._node(node, depth, out, last)

    def _node(self, node: Node, depth: int, out, last: bool):
        r, n = self.r, self.n
        kind = node.kind
        at = node.instr.offset if node.instr else None
        if kind == "stmt":
            out.append((at, depth, r.call(node.instr) if node.instr.code > 0x0A else statement(r, node.instr)))
        elif kind == "return":
            if not last:
                out.append((at, depth, "return"))
        elif kind == "label":
            out.append((None, max(depth - 1, 0), f"{n.label(node.target)}:"))
        elif kind == "goto":
            out.append((None, depth, f"goto {n.label(node.target)}"))
        elif kind in ("break", "continue"):
            out.append((None, depth, kind))
        elif kind == "ifgoto":
            cond = r.expr(node.instr.args[0][1])
            inner = node.then[0]
            word = inner.kind if inner.kind in ("break", "continue") else f"goto {n.label(inner.target)}"
            out.append((at, depth, f"if (!{_paren(cond)}) {word}"))
        elif kind == "if":
            cond = r.expr(node.instr.args[0][1])
            out.append((at, depth, f"if ({cond}) {{"))
            self._nodes(node.then, depth + 1, out)
            other = node.other
            while other is not None and len(other) == 1 and other[0].kind == "if":
                inner = other[0]
                out.append((inner.instr.offset, depth, f"}} else if ({r.expr(inner.instr.args[0][1])}) {{"))
                self._nodes(inner.then, depth + 1, out)
                other = inner.other
            if other is not None:
                out.append((None, depth, "} else {"))
                self._nodes(other, depth + 1, out)
            out.append((None, depth, "}"))
        elif kind == "while":
            out.append((at, depth, f"while ({r.expr(node.instr.args[0][1])}) {{"))
            self._nodes(node.then, depth + 1, out)
            out.append((None, depth, "}"))
        elif kind == "loop":
            out.append((None, depth, "loop {"))
            self._nodes(node.then, depth + 1, out)
            out.append((None, depth, "}"))
        elif kind == "switch":
            out.append((at, depth, f"switch ({r.expr(node.instr.args[0][1])}) {{"))
            for keys, body, _is_default in node.cases:
                for key in keys:
                    out.append((None, depth, "default:" if key is None else f"case {_case(key)}:"))
                self._nodes(body, depth + 1, out)
            out.append((None, depth, "}"))
        else:
            raise AssertionError(kind)


def routine_labels(program: Program, routine) -> set[int]:
    if routine.entry in program.routines and program.routines[routine.entry] is routine:
        return set(program.labels)
    return {t for i in routine.instrs for kind, t in i.targets if kind != "call"}


def _paren(text: str) -> str:
    return text if text.replace("_", "").replace("(", "").replace(")", "").isalnum() else f"({text})"


def text(program: Program) -> str:
    p = Pseudo(program)
    out = []
    for entry in sorted(program.routines):
        routine = program.routines[entry]
        lines, ok = p.routine(entry)
        out.append(f"\n{program.routine_name(entry)}() {{"
                   + ("" if ok else "   // not structured: flat listing"))
        for at, depth, line in lines:
            out.append(f"{'' if at is None else f'{at:05X}':>5}  {INDENT * depth}{line}")
        out.append("}")
    return "\n".join(out)
