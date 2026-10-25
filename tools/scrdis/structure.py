"""Recover if / else / while / switch from a routine's jumps.

The scripts were compiled by a simple one-pass compiler, so the shapes are
regular:

  if:      if_false_goto c, END;  then...;                     END:
  else:    if_false_goto c, ELSE; then...; goto END; ELSE: else...; END:
  while:   TOP: if_false_goto c, EXIT; body...; goto TOP;      EXIT:
  loop:    TOP: body...; goto TOP;                             EXIT:
  switch:  switch x {k: Lk ...; default: D}; Lk: body...; goto END; ...  END:

Anything that does not fit stays a labelled goto, so the output is always
complete. `check()` confirms every instruction is emitted exactly once; a
routine that fails it is rendered flat instead.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from .decode import Instr, Program

GOTOS = (0x03, 0x07, 0x09)


@dataclass
class Node:
    kind: str                    # stmt if while loop switch goto break continue return label
    instr: Instr | None = None
    then: list["Node"] = field(default_factory=list)
    other: list["Node"] | None = None
    cases: list[tuple[list[int], list["Node"], bool]] = field(default_factory=list)
    target: int = 0


@dataclass
class _Ctx:
    head: int | None = None      # continue target
    exit: int | None = None      # break target


class Structurer:
    def __init__(self, program: Program, instrs: list[Instr], labels: set[int]):
        self.program = program
        self.instrs = instrs
        self.labels = labels
        self.index = {i.offset: k for k, i in enumerate(instrs)}
        self.emitted: list[int] = []
        # Labels that some emitted goto still needs.
        self.needed: set[int] = set()

    # -- helpers ---------------------------------------------------------------

    def _at(self, offset: int) -> int | None:
        return self.index.get(offset)

    def _idx_before(self, offset: int, lo: int) -> int | None:
        """Index of the last instruction that ends exactly at `offset`."""
        k = self._at(offset)
        k = (k if k is not None else self._first_at_or_after(offset)) - 1
        if k >= lo and self.instrs[k].end == offset:
            return k
        return None

    def _first_at_or_after(self, offset: int) -> int:
        for k, instr in enumerate(self.instrs):
            if instr.offset >= offset:
                return k
        return len(self.instrs)

    def _back_edge(self, head: int, lo: int, hi: int) -> int | None:
        """The last unconditional goto in [lo, hi) jumping back to `head`."""
        found = None
        for k in range(lo, hi):
            instr = self.instrs[k]
            if instr.code in GOTOS and instr.targets[0][1] == head:
                found = k
        return found

    # -- the walk ----------------------------------------------------------------

    def run(self) -> list[Node]:
        return self.block(0, len(self.instrs), _Ctx())

    def block(self, lo: int, hi: int, ctx: _Ctx) -> list[Node]:
        out: list[Node] = []
        k = lo
        while k < hi:
            instr = self.instrs[k]
            if k > lo and instr.offset in self.labels:
                out.append(Node("label", target=instr.offset))
            consumed = self._loop(k, hi, ctx, out)
            if consumed is None:
                consumed = self._one(k, hi, ctx, out)
            k = consumed
        return out

    def _loop(self, k: int, hi: int, ctx: _Ctx, out: list[Node]) -> int | None:
        head = self.instrs[k].offset
        g = self._back_edge(head, k + 1, hi)
        if g is None:
            return None
        exit_ = self.instrs[g].end
        inner = _Ctx(head=head, exit=exit_)
        first = self.instrs[k]
        if first.code == 0x01 and first.targets[0][1] == exit_:
            self.emitted.append(k)
            self.emitted.append(g)
            out.append(Node("while", first, then=self.block(k + 1, g, inner)))
        else:
            self.emitted.append(g)
            out.append(Node("loop", then=self.block(k, g, inner)))
        return g + 1

    def _jump(self, target: int, ctx: _Ctx) -> Node:
        if ctx.exit is not None and target == ctx.exit:
            return Node("break", target=target)
        if ctx.head is not None and target == ctx.head:
            return Node("continue", target=target)
        self.needed.add(target)
        return Node("goto", target=target)

    def _one(self, k: int, hi: int, ctx: _Ctx, out: list[Node]) -> int:
        instr = self.instrs[k]
        code = instr.code
        hi_off = self.instrs[hi - 1].end if hi > 0 else 0

        if code == 0x01:
            target = instr.targets[0][1]
            t = self._at(target)
            if target > instr.offset and (t is not None and t <= hi or target == hi_off):
                t = t if t is not None else hi
                self.emitted.append(k)
                last = t - 1
                tail = self.instrs[last] if last > k else None
                if (tail is not None and tail.code in GOTOS
                        and tail.targets[0][1] > target
                        and tail.targets[0][1] not in (ctx.exit, ctx.head)):
                    end = tail.targets[0][1]
                    e = self._at(end)
                    if e is not None and e <= hi or end == hi_off:
                        e = e if e is not None else hi
                        self.emitted.append(last)
                        node = Node("if", instr, then=self.block(k + 1, last, ctx),
                                    other=self.block(t, e, ctx))
                        out.append(node)
                        return e
                out.append(Node("if", instr, then=self.block(k + 1, t, ctx)))
                return t
            self.emitted.append(k)
            out.append(Node("ifgoto", instr, then=[self._jump(target, ctx)]))
            return k + 1

        if code == 0x02:
            return self._switch(k, hi, ctx, out)

        self.emitted.append(k)
        if code in GOTOS:
            out.append(self._jump(instr.targets[0][1], ctx))
        elif code == 0x04:
            out.append(Node("return", instr))
        else:
            out.append(Node("stmt", instr))
        return k + 1

    def _switch(self, k: int, hi: int, ctx: _Ctx, out: list[Node]) -> int:
        instr = self.instrs[k]
        cases = instr.extra["cases"]
        default = instr.extra["default"]
        starts = sorted({t for _, t in cases} | {default})
        hi_off = self.instrs[hi - 1].end if hi > 0 else 0
        if any(t <= instr.offset or (self._at(t) is None and t != hi_off) for t in starts) \
                or any(self._at(t) is not None and self._at(t) > hi for t in starts):
            self.emitted.append(k)
            out.append(Node("stmt", instr))
            return k + 1

        # END: the break target. Each case body's trailing goto names it; with
        # no default clause the default target is END itself.
        last_start = starts[-1]
        breaks = []
        for start in starts[1:]:
            j = self._idx_before(start, k)
            if j is not None and self.instrs[j].code in GOTOS:
                target = self.instrs[j].targets[0][1]
                if target > last_start and target not in (ctx.exit, ctx.head):
                    breaks.append(target)
        end = max([default] + breaks)
        e = self._at(end)
        if e is None:
            if end != hi_off:
                self.emitted.append(k)
                out.append(Node("stmt", instr))
                return k + 1
            e = hi
        if e > hi:
            self.emitted.append(k)
            out.append(Node("stmt", instr))
            return k + 1

        self.emitted.append(k)
        inner = _Ctx(head=ctx.head, exit=end)
        bounds = [s for s in starts if s < end] + [end]
        groups = []
        for a, b in zip(bounds, bounds[1:]):
            keys = [key for key, t in cases if t == a]
            is_default = a == default
            ka, kb = self._at(a), self._at(b) if b != end else e
            groups.append((keys if not is_default else keys + [None], self.block(ka, kb, inner), is_default))
        node = Node("switch", instr, cases=[(keys, body, d) for keys, body, d in groups])
        out.append(node)
        return e


def structure(program: Program, instrs: list[Instr], labels: set[int] | None = None) -> list[Node] | None:
    if labels is None:
        labels = set(program.labels)
    s = Structurer(program, instrs, labels)
    try:
        nodes = s.run()
    except (IndexError, KeyError, TypeError):
        return None
    if sorted(s.emitted) != list(range(len(instrs))):
        return None
    # Drop labels nothing jumps to any more.
    used = s.needed

    def prune(nodes):
        kept = []
        for node in nodes:
            if node.kind == "label" and node.target not in used:
                continue
            node.then = prune(node.then)
            if node.other is not None:
                node.other = prune(node.other)
            node.cases = [(keys, prune(body), d) for keys, body, d in node.cases]
            kept.append(node)
        return kept

    return prune(nodes)
