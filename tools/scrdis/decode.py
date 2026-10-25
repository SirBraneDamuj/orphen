"""Decode a scene-script blob: instructions, expressions, and what points where.

The VM (FUN_0025bc68 statements, FUN_0025c258 expressions, FUN_0025bf70
literals) is described in port/src/ported/script/scene_command_interpreter.*;
this module follows that code, not the older notes in docs/.

Decoding is reachability-driven. Starting from the header entries, every
control edge and every operand that names a script body is followed, so only
bytes something can actually execute are read as code. A wrong operand width
shows up as a decode error or as a jump into the middle of an instruction,
and both are recorded rather than papered over.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

from .opcodes import ASSIGN_OPS, BINARY, METHODS, OPS, PRECEDENCE, REG_OPS, UNARY, Op, Operand

HEADER_WORDS = 11
EVENT_RECORD = 8
WORLD_SCALE = 100000.0   # fGpffff8c40: script value / 100000 = world units


class DecodeError(Exception):
    def __init__(self, offset: int, message: str):
        super().__init__(f"0x{offset:X}: {message}")
        self.offset = offset
        self.message = message


# ---------------------------------------------------------------- expressions

@dataclass
class Expr:
    """A node of a decoded expression. `kind` is lit, bin, un, stmt, rgb."""
    kind: str
    value: int | None = None        # lit: the value the VM computes
    token: int | None = None        # lit: 0x0C..0x11; bin/un: operator byte
    raw: int | None = None          # lit: the stored number before scaling
    children: list["Expr"] = field(default_factory=list)
    instr: "Instr | None" = None    # stmt: a statement evaluated for its value

    def const(self) -> int | None:
        return self.value if self.kind == "lit" else None


def _s32(v: int) -> int:
    return v - (1 << 32) if v & 0x80000000 else v


def _s16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


# ---------------------------------------------------------------- statements

@dataclass
class Instr:
    offset: int
    end: int
    code: int
    op: Op
    args: list[tuple[Operand, object]] = field(default_factory=list)
    # Control flow. `targets` are (label kind, offset); `falls` says whether
    # execution can continue at `end`.
    targets: list[tuple[str, int]] = field(default_factory=list)
    falls: bool = True
    # Extra decoded payloads: switch cases, preload ids, inline text range.
    extra: dict = field(default_factory=dict)


@dataclass
class Ref:
    """Something an operand points at, discovered while decoding."""
    kind: str          # code stream data pts3 pts2 dialogue
    target: int
    source: int        # offset of the instruction that names it
    note: str = ""


class Blob:
    def __init__(self, data: bytes):
        self.data = data
        if len(data) < HEADER_WORDS * 4:
            raise ValueError("blob too small for an SCR header")
        self.header = list(struct.unpack_from("<11I", data, 0))

    def __len__(self):
        return len(self.data)

    def u8(self, at: int) -> int:
        if not 0 <= at < len(self.data):
            raise DecodeError(at, "read past the end of the blob")
        return self.data[at]

    def u16(self, at: int) -> int:
        return self.u8(at) | (self.u8(at + 1) << 8)

    def u32(self, at: int) -> int:
        return self.u16(at) | (self.u16(at + 2) << 16)


class Decoder:
    def __init__(self, blob: Blob):
        self.blob = blob
        self.refs: list[Ref] = []

    # -- literals and expressions (FUN_0025bf70 / FUN_0025c258) -------------

    def expr(self, pos: int) -> tuple[Expr, int]:
        b = self.blob
        stack: list[Expr] = []
        start = pos
        while True:
            token = b.u8(pos)
            if token > 0x31:
                instr = self.stmt(pos, in_expr=True)
                stack.append(Expr("stmt", instr=instr))
                pos = instr.end
                continue
            if token == 0x0C:
                v = b.u8(pos + 1)
                stack.append(Expr("lit", v, token, v)); pos += 2
            elif token == 0x0D:
                v = b.u16(pos + 1)
                stack.append(Expr("lit", v, token, v)); pos += 3
            elif token == 0x0E:
                v = b.u32(pos + 1)
                stack.append(Expr("lit", v, token, v)); pos += 5
            elif token == 0x0F:
                raw = _s32(b.u32(pos + 1))
                stack.append(Expr("lit", (raw * 100) & 0xFFFFFFFF, token, raw)); pos += 5
            elif token == 0x10:
                raw = _s16(b.u16(pos + 1))
                stack.append(Expr("lit", (raw * 1000) & 0xFFFFFFFF, token, raw)); pos += 3
            elif token == 0x11:
                raw = _s16(b.u16(pos + 1))
                # C division truncates toward zero.
                q = abs(raw * 0xF570) // 0x168
                stack.append(Expr("lit", (q if raw >= 0 else -q) & 0xFFFFFFFF, token, raw)); pos += 3
            elif token in (0x30, 0x31):
                # Blue is evaluated first, then green, red, and alpha last.
                pos += 1
                parts = []
                for _ in range(4 if token == 0x31 else 3):
                    part, pos = self.expr(pos)
                    parts.append(part)
                stack.append(Expr("rgb", token=token, children=parts))
            elif token == 0x0B:
                pos += 1
                if not stack:
                    return Expr("lit", 0, 0x0C, 0), pos
                if len(stack) > 1:
                    raise DecodeError(start, f"expression left {len(stack)} values on the stack")
                return stack[0], pos
            elif token in UNARY:
                if not stack:
                    raise DecodeError(pos, f"unary 0x{token:02X} on an empty stack")
                stack[-1] = Expr("un", token=token, children=[stack[-1]]); pos += 1
            elif token in BINARY:
                if len(stack) < 2:
                    raise DecodeError(pos, f"binary 0x{token:02X} with {len(stack)} operand(s)")
                right = stack.pop()
                stack[-1] = Expr("bin", token=token, children=[stack[-1], right]); pos += 1
            else:
                raise DecodeError(pos, f"byte 0x{token:02X} is not an expression token")

    # -- statements (FUN_0025bc68 and the dispatch tables) -------------------

    def rel(self, at: int) -> int:
        """FUN_0025c220: the target of the rel32 stored at `at`."""
        target = at + _s32(self.blob.u32(at))
        if not 0 <= target < len(self.blob):
            raise DecodeError(at, f"jump to 0x{target:X} is outside the blob")
        return target

    def stmt(self, pos: int, in_expr: bool = False) -> Instr:
        b = self.blob
        code = b.u8(pos)
        start = pos

        if code < 0x0B and not in_expr:
            return self._structural(pos, code)
        if code <= 0x31:
            raise DecodeError(pos, f"byte 0x{code:02X} is not a statement")
        if code == 0xFF:
            code = 0x100 + b.u8(pos + 1)
            pos += 2
        else:
            pos += 1
        op = OPS.get(code)
        if op is None:
            raise DecodeError(start, f"opcode 0x{code:X} has no handler")

        if code == 0x32:
            if in_expr:
                # PTR_LAB_0031e228[0]: `jr ra; move v0, zero`, no operands.
                return Instr(start, pos, code, op, extra={"zero": True})
            target = self.rel(pos)
            self.refs.append(Ref("code", target, start, "call"))
            return Instr(start, pos + 4, code, op, targets=[("call", target)])

        if code == 0x33:
            # [rel32 skip][text...]: the text starts after the rel32 and the
            # jump lands past it.
            resume = self.rel(pos)
            self.refs.append(Ref("say", pos + 4, start))
            return Instr(start, resume, code, op, extra={"text": (pos + 4, resume)})

        if code == 0x4D:
            count = b.u8(pos)
            pos += 1
            ids = []
            for _ in range(count):
                ids.append(b.u32(pos))
                pos += 4
            return Instr(start, pos, code, op, extra={"ids": ids})

        args = []
        for operand in op.operands:
            if operand.kind == "e":
                value, pos = self.expr(pos)
            elif operand.kind == "b":
                value = b.u8(pos); pos += 1
            elif operand.kind == "h":
                value = b.u16(pos); pos += 2
            elif operand.kind == "w":
                value = b.u32(pos); pos += 4
            else:
                raise AssertionError(operand.kind)
            args.append((operand, value))
        instr = Instr(start, pos, code, op, args)
        self._collect_refs(instr)
        return instr

    def _structural(self, pos: int, code: int) -> Instr:
        op = OPS[code]
        p = pos + 1
        if code == 0x04:
            return Instr(pos, p, code, op, falls=False)
        if code in (0x00, 0x05, 0x0A):
            return Instr(pos, p, code, op)
        if code in (0x06, 0x08):
            return Instr(pos, p + 4, code, op, extra={"skipped": self.blob.u32(p)})
        if code in (0x03, 0x07, 0x09):
            return Instr(pos, p + 4, code, op, targets=[("goto", self.rel(p))], falls=False)
        if code == 0x01:
            cond, p = self.expr(p)
            return Instr(pos, p + 4, code, op, [(Operand("e", "condition"), cond)],
                         targets=[("else", self.rel(p))])
        if code == 0x02:
            selector, p = self.expr(p)
            count = self.blob.u8(p)
            p += 1
            p = (p + 3) & ~3
            cases = []
            for _ in range(count):
                key = self.blob.u32(p)
                cases.append((key, self.rel(p + 4)))
                p += 8
            default = self.rel(p)
            targets = [("case", t) for _, t in cases] + [("default", default)]
            return Instr(pos, p + 4, code, op, [(Operand("e", "selector"), selector)],
                         targets=targets, falls=False,
                         extra={"cases": cases, "default": default})
        raise AssertionError(code)

    def _collect_refs(self, instr: Instr):
        for operand, value in instr.args:
            if operand.ref is None:
                continue
            target = value.const() if isinstance(value, Expr) else value
            if target is None:
                continue
            if operand.ref in ("code", "stream", "data", "pts3", "pts2") and 0 < target < len(self.blob):
                self.refs.append(Ref(operand.ref, target, instr.offset, f"{instr.op.name}.{operand.name}"))
        if instr.code == 0xBD:
            method = instr.args[1][1].const()
            path = instr.args[2][1].const()
            if method == 0x70 and path is not None and 0 < path < len(self.blob):
                self.refs.append(Ref("pts3", path, instr.offset, "method.path_follow"))


# ---------------------------------------------------------------- the program

@dataclass
class Routine:
    entry: int
    names: list[str]
    instrs: list[Instr] = field(default_factory=list)
    error: DecodeError | None = None
    refs: list[Ref] = field(default_factory=list)   # only set on speculative bodies

    @property
    def subproc_id(self) -> int | None:
        return None


@dataclass
class EventStream:
    offset: int
    records: list[tuple[int, int, int, int]]   # (offset, delay, gate, target)
    armed_by: list[int]


@dataclass
class PointList:
    offset: int
    width: int
    points: list[list[Expr]]
    used_by: list[int]
    error: str = ""


class Program:
    """Everything decoded out of one blob."""

    def __init__(self, data: bytes):
        self.blob = Blob(data)
        self.decoder = Decoder(self.blob)
        self.instrs: dict[int, Instr] = {}
        self.owner: dict[int, int] = {}            # byte offset -> instruction start
        self.routines: dict[int, Routine] = {}
        self.labels: dict[int, set[str]] = {}
        self.conflicts: list[tuple[int, str]] = []
        self.streams: dict[int, EventStream] = {}
        self.point_lists: dict[int, PointList] = {}
        self.data_refs: dict[int, list[Ref]] = {}
        self.dialogue_table: list[int] = []
        self.dialogue_window = (0, 0)
        self._decode()

    # -- header and dialogue table (FUN_0025b288) -----------------------------

    def _read_dialogue_table(self):
        start = self.blob.header[5]
        if not 0 < start < len(self.blob):
            return
        entries = []
        at = start
        while at + 4 <= len(self.blob):
            value = self.blob.u32(at)
            if value == 0:
                break
            entries.append(value)
            at += 4
        self.dialogue_table = entries
        if entries:
            # First and last non-zero entries; the top bound is exclusive, so
            # the final entry is treated as script by the scheduler.
            self.dialogue_window = (entries[0], entries[-1])

    def is_dialogue_target(self, offset: int) -> bool:
        first, last = self.dialogue_window
        return first <= offset < last

    # -- reachability ---------------------------------------------------------

    def _decode(self):
        self._read_dialogue_table()
        roots: list[tuple[int, str]] = []
        names = ["init", "start", "tick", "interact", "teardown"]
        for index, name in enumerate(names):
            entry = self.blob.header[index]
            if 0 < entry < len(self.blob):
                roots.append((entry, name))

        pending = list(roots)
        while pending:
            entry, name = pending.pop(0)
            routine = self.routines.get(entry)
            if routine is not None:
                if name not in routine.names:
                    routine.names.append(name)
                continue
            routine = Routine(entry, [name])
            self.routines[entry] = routine
            self._walk(routine)
            pending.extend(self._new_roots())

        for routine in self.routines.values():
            routine.instrs.sort(key=lambda i: i.offset)

    _consumed_refs = 0

    def _new_roots(self) -> list[tuple[int, str]]:
        out = []
        refs = self.decoder.refs[self._consumed_refs:]
        self._consumed_refs = len(self.decoder.refs)
        for ref in refs:
            if ref.kind == "code":
                out.append((ref.target, "sub" if ref.note == "call" else ref.note))
            elif ref.kind == "stream":
                out.extend(self._read_stream(ref))
            elif ref.kind in ("pts3", "pts2"):
                self._read_point_list(ref)
            elif ref.kind == "data":
                self.data_refs.setdefault(ref.target, []).append(ref)
        return out

    def _walk(self, routine: Routine):
        work = [routine.entry]
        self._label(routine.entry, "entry")
        while work:
            pos = work.pop()
            while True:
                if pos in self.instrs:
                    instr = self.instrs[pos]
                    if instr not in routine.instrs:
                        # Shared tail with another routine: fine, stop here.
                        pass
                    break
                if pos in self.owner:
                    self.conflicts.append((pos, f"jump into the middle of the instruction at 0x{self.owner[pos]:X}"))
                    break
                try:
                    instr = self.decoder.stmt(pos)
                except DecodeError as error:
                    routine.error = routine.error or error
                    self.conflicts.append((error.offset, error.message))
                    break
                self.instrs[pos] = instr
                for at in range(instr.offset, instr.end):
                    self.owner.setdefault(at, instr.offset)
                routine.instrs.append(instr)
                for kind, target in instr.targets:
                    if kind == "call":
                        continue
                    self._label(target, kind)
                    work.append(target)
                if not instr.falls:
                    break
                pos = instr.end

    def _label(self, offset: int, kind: str):
        self.labels.setdefault(offset, set()).add(kind)

    # -- event streams (FUN_0025ce30) -----------------------------------------

    def _read_stream(self, ref: Ref) -> list[tuple[int, str]]:
        existing = self.streams.get(ref.target)
        if existing is not None:
            existing.armed_by.append(ref.source)
            return []
        records = []
        at = ref.target
        roots = []
        while at + EVENT_RECORD <= len(self.blob):
            delay = self.blob.u16(at)
            gate = self.blob.u16(at + 2)
            target = self.blob.u32(at + 4)
            # The stream ends where the next record's target is zero.
            if target == 0:
                break
            records.append((at, delay, gate, target))
            if not self.is_dialogue_target(target) and 0 < target < len(self.blob):
                roots.append((target, "event"))
            at += EVENT_RECORD
        self.streams[ref.target] = EventStream(ref.target, records, [ref.source])
        return roots

    # -- point lists (FUN_0025d618 and its callers) ----------------------------

    def _read_point_list(self, ref: Ref):
        width = 3 if ref.kind == "pts3" else 2
        existing = self.point_lists.get(ref.target)
        if existing is not None:
            existing.used_by.append(ref.source)
            return
        plist = PointList(ref.target, width, [], [ref.source])
        self.point_lists[ref.target] = plist
        try:
            count = self.blob.u32(ref.target)
            if count > 16:
                plist.error = f"count {count} is over FUN_00266a78's limit of 16"
                return
            pos = ref.target + 4
            for _ in range(count):
                point = []
                for _ in range(width):
                    value, pos = self.decoder.expr(pos)
                    point.append(value)
                plist.points.append(point)
            plist.end = pos
        except DecodeError as error:
            plist.error = error.message

    # -- header-derived data blocks ---------------------------------------------

    def header_blocks(self) -> list[tuple[int, int, str, str]]:
        """(begin, end, kind, description) for the blocks the header names."""
        h = self.blob.header
        size = len(self.blob)
        out = []
        if 0 < h[6] < size:
            # FUN_0025b600: a skipped halfword, sixteen texture-slot halfwords,
            # an always-advancing realign, then spawn xyz and draw distance as
            # ints over 1000.
            at = h[6] + 2 + 32
            at += (4 - (at & 3)) // 2 * 2
            out.append((h[6], min(at + 16, size), "defaults",
                        "scene defaults: texture slot table, spawn point, draw distance (FUN_0025b600)"))
        if 0 < h[7] < size:
            entry0 = self.blob.u32(h[7]) if h[7] + 4 <= size else 0
            out.append((h[7], h[7] + 4, "sections", "section table (FUN_0025ba28); entry 0 is the battle encounter"))
            if 0 < entry0 < size:
                out.append((entry0, entry0, "encounter",
                            "battle encounter data (FUN_0023f318): actor arrays and FUN_0023FD30 battle-VM scripts, not decoded here"))
        if 0 < h[10] < size:
            out.append((h[10], min(h[10] + 16, size), "music",
                        "eight u16 music requests; bit 15 starts the slot (FUN_0025b2f0)"))
        return out

    def scene_defaults(self) -> dict | None:
        h6 = self.blob.header[6]
        if not 0 < h6 < len(self.blob):
            return None
        slots = [self.blob.u16(h6 + 2 + 2 * k) for k in range(16)]
        at = h6 + 2 + 32
        at += (4 - (at & 3)) // 2 * 2
        if at + 16 > len(self.blob):
            return {"slots": slots}
        ints = [_s32(self.blob.u32(at + 4 * k)) for k in range(4)]
        return {"slots": slots, "spawn": [v / 1000 for v in ints[:3]], "draw_distance": ints[3] / 1000}

    def music_requests(self) -> list[int]:
        h10 = self.blob.header[10]
        if not 0 < h10 or h10 + 16 > len(self.blob):
            return []
        return [self.blob.u16(h10 + 2 * k) for k in range(8)]

    # -- speculative sweep of what nothing reaches --------------------------------

    def sweep(self, claimed: set[int]) -> dict[int, "Routine"]:
        """Linear-sweep the gaps for bodies that decode cleanly to an `end`.

        Nothing reachable names these, so they are only candidates: the engine
        may start them from data this tool does not parse (an entity type's
        handler, the battle VM), or they may be dead. They are decoded with a
        throwaway decoder so they cannot leak into the verified program.
        """
        size = len(self.blob)
        found: dict[int, Routine] = {}
        taken: set[int] = set()
        at = HEADER_WORDS * 4
        while at < size:
            if at in claimed or at in taken:
                at += 1
                continue
            # Try the body at `at`, and after a subproc id dword at `at`.
            for start in (at, at + 4):
                routine = self._try_body(start, claimed | taken)
                if routine is not None:
                    found[start] = routine
                    for instr in routine.instrs:
                        taken.update(range(instr.offset, instr.end))
                    at = max(i.end for i in routine.instrs)
                    break
            else:
                at += 1
        return found

    def _try_body(self, start: int, claimed: set[int]) -> "Routine | None":
        if start >= len(self.blob) or start in claimed:
            return None
        first = self.blob.data[start]
        # A body opens with a statement; 0x04 alone or padding is not one.
        if first in (0x00, 0x04) or 0x0B <= first <= 0x31:
            return None
        decoder = Decoder(self.blob)
        seen: dict[int, Instr] = {}
        work = [start]
        while work:
            pos = work.pop()
            while pos not in seen:
                if pos in claimed:
                    return None
                try:
                    instr = decoder.stmt(pos)
                except DecodeError:
                    return None
                if any(b in claimed for b in range(instr.offset, instr.end)):
                    return None
                seen[pos] = instr
                for kind, target in instr.targets:
                    if kind != "call":
                        work.append(target)
                if not instr.falls:
                    break
                pos = instr.end
            if len(seen) > 4000:
                return None
        instrs = sorted(seen.values(), key=lambda i: i.offset)
        if len(instrs) < 2 or not any(i.code == 0x04 for i in instrs):
            return None
        # Reject sweeps that are not contiguous: real bodies are laid out in one run.
        for a, b in zip(instrs, instrs[1:]):
            if b.offset < a.end or b.offset - a.end > 8:
                return None
        routine = Routine(start, ["unreferenced"])
        routine.instrs = instrs
        routine.refs = decoder.refs
        return routine

    # -- helpers for renderers --------------------------------------------------

    def subproc_id(self, entry: int) -> int | None:
        """The authored id in the dword in front of a slot body (FUN_0025b778:23)."""
        if entry < 4:
            return None
        return self.blob.u32(entry - 4)

    def routine_name(self, entry: int) -> str:
        routine = self.routines.get(entry)
        names = routine.names if routine else []
        for fixed in ("init", "start", "tick", "interact", "teardown"):
            if fixed in names:
                return fixed
        if "sub" in names and len(names) == 1:
            return f"sub_{entry:04X}"
        return f"body_{entry:04X}"
