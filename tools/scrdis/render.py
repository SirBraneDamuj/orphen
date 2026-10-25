"""Turn decoded instructions and expressions into readable text.

Everything here is presentation. The decoded values are untouched; the
renderer only chooses how to spell them, and every spelling is reversible
from the legend in the README (and the tooltips in the HTML view).
"""
from __future__ import annotations

from .decode import Expr, Instr, Program
from .opcodes import ASSIGN_OPS, BINARY, METHODS, PRECEDENCE, REG_OPS, UNARY

# Operand names whose values read better in hex.
HEX_NAMES = {"id", "mask", "flags", "bits", "colour", "colour_bits", "type", "cue",
             "record", "model", "bank", "value3", "tag", "nibble"}
# Operand names that go through FUN_0025d6c0, where 0x100 keeps the selection.
SELECTOR_NAMES = {"entity", "parent", "child"}
CURRENT_ENTITY = 0x100


class Namer:
    """How offsets are spelled. The HTML renderer overrides `link`."""

    def __init__(self, program: Program):
        self.program = program

    def link(self, kind: str, offset: int, text: str) -> str:
        return text

    def code(self, offset: int) -> str:
        return self.link("code", offset, self.program.routine_name(offset))

    def label(self, offset: int) -> str:
        return self.link("label", offset, f"L_{offset:04X}")

    def stream(self, offset: int) -> str:
        return self.link("stream", offset, f"stream_{offset:04X}")

    def data(self, offset: int, kind: str = "data") -> str:
        return self.link(kind, offset, f"{kind}_{offset:04X}")

    def dialogue(self, offset: int) -> str:
        return self.link("dialogue", offset, f"msg_{offset:04X}")

    def esc(self, text: str) -> str:
        return text


def _number(value: int, hexy: bool) -> str:
    signed = value - (1 << 32) if value & 0x80000000 else value
    if hexy and signed >= 10:
        return f"0x{value:X}"
    if abs(signed) >= 0x10000:
        return f"0x{value:X}" if signed > 0 else str(signed)
    return str(signed)


def _world(raw: int) -> str:
    # 0x0F stores raw * 100 and the world scale is 100000, so raw / 1000.
    text = f"{raw / 1000:.3f}".rstrip("0")
    return text + "0" if text.endswith(".") else text


class Renderer:
    def __init__(self, program: Program, namer: Namer | None = None):
        self.program = program
        self.n = namer or Namer(program)

    # -- expressions ----------------------------------------------------------

    def expr(self, e, hint: str = "", ref: str | None = None) -> str:
        return self._expr(e, hint, ref)[0]

    def _expr(self, e: Expr, hint: str, ref: str | None) -> tuple[str, int]:
        """(text, precedence); precedence 100 means atomic."""
        if e.kind == "lit":
            if ref and e.value:
                linked = self._ref(ref, e.value)
                if linked:
                    return linked, 100
            if hint in SELECTOR_NAMES and e.value == CURRENT_ENTITY:
                return "current", 100
            if e.token == 0x0F:
                return _world(e.raw), 100
            if e.token == 0x11:
                return f"{e.raw}deg", 100
            return _number(e.value, hint in HEX_NAMES), 100
        if e.kind == "rgb":
            parts = [self.expr(c) for c in e.children]
            b, g, r = parts[:3]
            inner = [r, g, b] + parts[3:]
            return f"{'rgba' if e.token == 0x31 else 'rgb'}({', '.join(inner)})", 100
        if e.kind == "un":
            text, prec = self._expr(e.children[0], hint, None)
            if prec < 100:
                text = f"({text})"
            return f"{UNARY[e.token]}{text}", 95
        if e.kind == "bin":
            op = BINARY[e.token]
            prec = PRECEDENCE[op]
            left, lp = self._expr(e.children[0], hint, None)
            right, rp = self._expr(e.children[1], hint, None)
            if lp < prec:
                left = f"({left})"
            if rp <= prec:
                right = f"({right})"
            return f"{left} {op} {right}", prec
        if e.kind == "stmt":
            return self.call(e.instr), 100
        raise AssertionError(e.kind)

    def _ref(self, ref: str, target: int) -> str | None:
        if ref == "code":
            return self.n.code(target)
        if ref == "stream":
            return self.n.stream(target)
        if ref in ("pts3", "pts2"):
            return self.n.data(target, "path")
        if ref == "data":
            return self.n.data(target)
        return None

    # -- one instruction as a call or assignment --------------------------------

    def call(self, instr: Instr) -> str:
        code = instr.code
        op = instr.op
        args = instr.args

        def arg(i: int) -> str:
            operand, value = args[i]
            if isinstance(value, Expr):
                return self.expr(value, operand.name, operand.ref)
            if operand.ref:
                linked = self._ref(operand.ref, value)
                if linked:
                    return linked
            return _number(value, operand.name in HEX_NAMES or operand.kind == "w")

        if code in (0x36, 0x38):
            return f"{'work' if code == 0x36 else 'flag_byte'}[{arg(0)}]"
        if code in (0x37, 0x39):
            target = f"{'work' if code == 0x37 else 'flag_byte'}[{arg(0)}]"
            sub = args[2][1]
            symbol = ASSIGN_OPS.get(sub)
            if symbol is None:
                return f"{target} ?op{sub:02X}? {arg(1)}"
            if symbol in ("++", "--"):
                value = args[1][1]
                return f"{target}{symbol}" if value.const() == 0 else f"{target}{symbol} /* {arg(1)} */"
            return f"{target} {symbol} {arg(1)}"
        if code == 0x76:
            return f"reg({arg(0)}, {arg(1)})"
        if code in REG_OPS:
            return f"reg({arg(0)}, {arg(1)}) {REG_OPS[code]} {arg(2)}"
        if code == 0x32:
            if instr.extra.get("zero"):
                return "zero()"
            return f"{self.n.code(instr.targets[0][1])}()"
        if code == 0x33:
            begin, end = instr.extra["text"]
            from .text import render_inline
            return f"say({render_inline(self.program, begin, end, self.n)})"
        if code == 0x4D:
            ids = ", ".join(f"0x{i:X}" for i in instr.extra["ids"])
            return f"preload_models({ids})"
        if code == 0xBD:
            method = args[1][1].const()
            name = METHODS.get(method, f"method_0x{method:X}" if method is not None else None)
            if name is not None:
                rest = [arg(0), arg(2), arg(3)]
                if method == 0x70:
                    path = args[2][1].const()
                    if path:
                        rest[1] = self.n.data(path, "path")
                return f"{name}({', '.join(rest)})"
        if code == 0x6D and args:
            mode = args[0][1]
            return f"player_lock({mode - 256 if mode & 0x80 else mode})"
        return f"{op.name}({', '.join(arg(i) for i in range(len(args)))})"
