"""A self-contained HTML explorer for one scene script."""
from __future__ import annotations

import html
import re
from collections import defaultdict

from . import known as K
from . import text as T
from .decode import Expr, Program
from .listing import statement
from .opcodes import HEADER_WORDS, OPS
from .pseudo import INDENT, Pseudo, routine_labels
from .render import Namer, Renderer

ANCHOR = {"code": "c", "label": "l", "stream": "s", "dialogue": "m", "path": "p", "data": "d", "instr": "i", "record": "r"}


class HtmlNamer(Namer):
    """Links come out as placeholders so the surrounding text can be escaped
    in one pass; `finish()` swaps them for anchors."""

    def __init__(self, program: Program):
        super().__init__(program)
        self.slots: list[str] = []
        # The flat view has a line per instruction and no label lines, so its
        # labels point at those lines instead.
        self.flat = False

    def link(self, kind: str, offset: int, text: str) -> str:
        anchor = "f" if kind == "label" and self.flat else ANCHOR.get(kind, "d")
        self.slots.append(f'<a class="ref {kind}" href="#{anchor}-{offset:X}">{html.escape(text)}</a>')
        return f"\x00{len(self.slots) - 1}\x00"

    def esc(self, text: str) -> str:
        return text

    def finish(self, text: str) -> str:
        escaped = html.escape(text)
        escaped = re.sub(r"\b(\d+\.\d+|-?\d+deg)\b", r'<span class="lit">\1</span>', escaped)
        return re.sub("\x00(\\d+)\x00", lambda m: self.slots[int(m.group(1))], escaped)


def _hex(data: bytes, begin: int, end: int) -> str:
    return data[begin:end].hex(" ")


class Explorer:
    def __init__(self, program: Program, title: str, scene: tuple[int, int] | None = None):
        self.p = program
        self.title = title
        self.scene = scene   # (section, entry) when read off the disc, for known.py
        self.n = HtmlNamer(program)
        self.r = Renderer(program, self.n)
        self.pseudo = Pseudo(program, self.n)
        self.routine_of: dict[int, int] = {}
        for entry, routine in program.routines.items():
            for instr in routine.instrs:
                self.routine_of[instr.offset] = entry
        self.xrefs = self._xrefs()
        self.messages = self._messages()
        claimed = {a for a0, b0, k in self.coverage() if k != "unclaimed" for a in range(a0, b0)}
        self.speculative = program.sweep(claimed)
        for routine in self.speculative.values():
            for ref in routine.refs:
                if ref.kind in ("pts3", "pts2") and ref.target not in program.point_lists:
                    program._read_point_list(ref)
                    program.point_lists[ref.target].speculative = True

    # -- cross references -----------------------------------------------------

    def _where(self, source: int) -> int | None:
        start = self.p.owner.get(source, source)
        return self.routine_of.get(start)

    def _xrefs(self) -> dict[tuple[str, int], list[tuple[str, int]]]:
        """(kind, target) -> [(how, source offset)]."""
        x = defaultdict(list)
        for ref in self.p.decoder.refs:
            kind = {"pts3": "path", "pts2": "path", "say": "say"}.get(ref.kind, ref.kind)
            x[(kind, ref.target)].append((ref.note or ref.kind, ref.source))
        for stream in self.p.streams.values():
            for at, delay, gate, target in stream.records:
                kind = "dialogue" if self.p.is_dialogue_target(target) else "code"
                x[(kind, target)].append((f"event @ stream_{stream.offset:04X}", at))
        return x

    def _messages(self) -> dict[int, T.Message]:
        table = sorted(set(self.p.dialogue_table))
        out = {}
        for k, offset in enumerate(table):
            limit = table[k + 1] if k + 1 < len(table) else None
            out[offset] = T.parse(self.p.blob.data, offset, limit)
        return out

    def _backlinks(self, kind: str, offset: int) -> str:
        refs = self.xrefs.get((kind, offset), [])
        if not refs:
            return ""
        parts = []
        for how, source in refs[:40]:
            where = self._where(source)
            if where is not None:
                place = self.n.code(where)
                link = self.n.link("instr", self.p.owner.get(source, source), f"0x{source:X}")
                parts.append(f"{how} in {place} at {link}")
            else:
                parts.append(f"{how} at {self.n.link('record', source, f'0x{source:X}')}"
                             if how.startswith("event") else f"{how} at 0x{source:X}")
        more = f" (+{len(refs) - 40} more)" if len(refs) > 40 else ""
        return '<div class="xref">' + self.n.finish("; ".join(parts) + more) + "</div>"

    # -- sections -------------------------------------------------------------

    def _routine(self, entry: int, routine=None) -> str:
        p = self.p
        speculative = routine is not None
        routine = routine or p.routines[entry]
        name = f"unref_{entry:04X}" if speculative else p.routine_name(entry)
        sid = p.subproc_id(entry)
        tags = sorted(set(routine.names))
        meta = ", ".join(tags)
        if sid is not None and sid < 0x10000 and not any(t in tags for t in ("init", "start", "tick", "interact", "teardown")):
            meta += f" &middot; subproc id {sid}"
        known = K.lookup(self.scene, entry) if speculative else None
        badge = f' <span class="badge">{html.escape(known.label)}</span>' if known else ""
        lines, ok = self.pseudo.routine(entry, routine)
        by_offset = {i.offset: i for i in routine.instrs}
        rows = []
        for at, depth, line in lines:
            anchor = ""
            if at is not None:
                anchor = f' id="i-{at:X}"'
                off = f'<a class="off" href="#i-{at:X}">{at:05X}</a>'
            else:
                off = '<span class="off"></span>'
                m = re.match(r"\x00(\d+)\x00:$", line)
                target = m and re.search(r'href="#l-([0-9A-F]+)"', self.n.slots[int(m.group(1))])
                if target:
                    anchor = f' id="l-{target.group(1)}"'
            title = ""
            instr = by_offset.get(at) if at is not None else None
            if instr is not None:
                op = instr.op
                title = (f' title="{op.name} (0x{instr.code:X}, handler FUN_{op.handler:08X}, '
                         f'shape from {op.source}){(" &#10;" + html.escape(op.note)) if op.note else ""}'
                         f'&#10;{_hex(p.blob.data, instr.offset, min(instr.end, instr.offset + 48))}"')
            rows.append(f'<div class="ln"{anchor}{title}>{off}<span class="code">{INDENT * depth}{self.n.finish(line)}</span></div>')
        flat_rows = []
        labels = routine_labels(p, routine)
        self.n.flat = True
        for instr in routine.instrs:
            if instr.offset != entry and instr.offset in labels:
                flat_rows.append(f'<div class="ln"><span class="off"></span><span class="code">'
                                 f'{self.n.finish(self.n.label(instr.offset))}:</span></div>')
            flat_rows.append(f'<div class="ln" id="f-{instr.offset:X}" '
                             f'title="{_hex(p.blob.data, instr.offset, min(instr.end, instr.offset + 48))}">'
                             f'<span class="off">{instr.offset:05X}</span><span class="code">{INDENT}'
                             f'{self.n.finish(statement(self.r, instr))}</span></div>')
        self.n.flat = False
        if routine.error:
            rows.append(f'<div class="err">decode stopped: {html.escape(str(routine.error))}</div>')
        body = "\n".join(rows)
        cls = "routine speculative" if speculative else "routine"
        note = f'<div class="known">{known.note}</div>' if known else ""
        return (f'<section class="{cls}" id="c-{entry:X}" data-name="{name} {meta}{" " + html.escape(known.label) if known else ""}">'
                f'<h3>{name}{badge}<span class="meta">{meta} &middot; 0x{entry:X}..0x{routine.instrs[-1].end if routine.instrs else entry:X}'
                f'{"" if ok else " &middot; flat"}</span>'
                f'<button class="toggle" type="button">flat</button></h3>'
                f'{self._backlinks("code", entry)}'
                f'{note}'
                f'<div class="lines structured">{body}</div>'
                f'<div class="lines flat" hidden>{"".join(flat_rows)}</div></section>')

    def _unref_tag(self, entry: int, routine) -> str:
        known = K.lookup(self.scene, entry)
        return html.escape(known.label) if known else str(len(routine.instrs))

    def _gate(self, gate: int) -> str:
        if gate == 0:
            return "&mdash;"
        if gate & 0x8000:
            return f"gate bits 0x{gate & 0x7FFF:X}"
        return f"flag 0x{gate:X}"

    def _stream(self, stream) -> str:
        rows = []
        total = 0
        for at, delay, gate, target in stream.records:
            total += delay
            if self.p.is_dialogue_target(target):
                msg = self.messages.get(target)
                preview = ""
                if msg is not None:
                    preview = (f"<b>{html.escape(msg.speaker)}</b> " if msg.speaker else "") + html.escape(T.plain(msg)[:120])
                what = self.n.finish(self.n.dialogue(target)) + f' <span class="say">{preview}</span>'
            else:
                what = self.n.finish(self.n.code(target))
            rows.append(f'<tr id="r-{at:X}"><td class="off">{at:05X}</td><td class="num">{delay}</td>'
                        f'<td>{self._gate(gate)}</td><td>{what}</td></tr>')
        return (f'<section class="stream" id="s-{stream.offset:X}" data-name="stream_{stream.offset:04X}">'
                f'<h3>stream_{stream.offset:04X}<span class="meta">{len(stream.records)} records &middot; '
                f'&ge;{total} frames of delay</span></h3>{self._backlinks("stream", stream.offset)}'
                f'<table><thead><tr><th>at</th><th>wait</th><th>gate</th><th>then</th></tr></thead>'
                f'<tbody>{"".join(rows)}</tbody></table></section>')

    def _message(self, offset: int, msg: T.Message) -> str:
        parts = []
        for piece in msg.pieces:
            if piece.kind == "text":
                parts.append(f'<span class="txt">{html.escape(piece.text)}</span>')
            elif piece.kind == "speaker":
                parts.append(f'<span class="spk">{html.escape(piece.text)}</span>')
            elif piece.kind == "choice":
                opts = " | ".join(html.escape(o) for o in piece.options)
                parts.append(f'<span class="cc choice">choice work[{piece.operands[0]}]: {opts}</span>')
                if piece.code == 0x07:
                    parts.append("<br>")
            elif piece.kind == "call":
                parts.append(f'<span class="cc">call {self.n.finish(self.n.dialogue(piece.target))}</span>')
            else:
                parts.append(f'<span class="cc">{html.escape(T.code_text(piece))}</span>')
                if piece.code == 0x07:
                    parts.append("<br>")
        return (f'<section class="msg" id="m-{offset:X}" data-name="msg_{offset:04X} {html.escape(msg.speaker)} {html.escape(T.plain(msg))}">'
                f'<h3>msg_{offset:04X}<span class="meta">{html.escape(msg.speaker) or "no speaker"} &middot; '
                f'{msg.end - offset} bytes</span></h3>{self._backlinks("dialogue", offset)}'
                f'<div class="text">{"".join(parts)}</div></section>')

    def _path(self, plist) -> str:
        rows = []
        for k, point in enumerate(plist.points):
            cells = "".join(f"<td>{self.n.finish(self.r.expr(v))}</td>" for v in point)
            rows.append(f"<tr><td class='num'>{k}</td>{cells}</tr>")
        head = "<th>#</th>" + ("<th>x</th><th>y</th><th>z</th>" if plist.width == 3 else "<th>roll</th><th>zoom</th>")
        err = f'<div class="err">{html.escape(plist.error)}</div>' if plist.error else ""
        if getattr(plist, "speculative", False):
            err += '<div class="note">named only by unreferenced code</div>'
        return (f'<section class="path" id="p-{plist.offset:X}" data-name="path_{plist.offset:04X}">'
                f'<h3>path_{plist.offset:04X}<span class="meta">{len(plist.points)} points</span></h3>'
                f'{self._backlinks("path", plist.offset)}{err}'
                f'<table><thead><tr>{head}</tr></thead><tbody>{"".join(rows)}</tbody></table></section>')

    # -- coverage ---------------------------------------------------------------

    def coverage(self) -> list[tuple[int, int, str]]:
        size = len(self.p.blob)
        kind = [""] * size

        def mark(a, b, k):
            for i in range(max(a, 0), min(b, size)):
                if not kind[i]:
                    kind[i] = k

        mark(0, 44, "header")
        for at in self.p.owner:
            kind[at] = kind[at] or "code"
        for offset, msg in self.messages.items():
            mark(offset, msg.end, "text")
        h5 = self.p.blob.header[5]
        mark(h5, h5 + 4 * (len(self.p.dialogue_table) + 1), "table")
        for stream in self.p.streams.values():
            mark(stream.offset, stream.offset + 8 * (len(stream.records) + 1), "stream")
        for plist in self.p.point_lists.values():
            mark(plist.offset, getattr(plist, "end", plist.offset + 4), "path")
        for routine in self.p.routines.values():
            if routine.entry >= 4 and routine.names != ["sub"]:
                mark(routine.entry - 4, routine.entry, "id")
        for begin, end, k, _ in self.p.header_blocks():
            mark(begin, end, k)
        for routine in getattr(self, "speculative", {}).values():
            for instr in routine.instrs:
                mark(instr.offset, instr.end, "speculative")
        runs = []
        start = 0
        for i in range(1, size + 1):
            if i == size or kind[i] != kind[start]:
                runs.append((start, i, kind[start] or "unclaimed"))
                start = i
        return runs

    def _coverage_section(self, runs) -> str:
        size = len(self.p.blob)
        dumped = {a for a, b, k in runs if k == "unclaimed" and any(self.p.blob.data[a:b])}
        bars = "".join(
            f'<a class="seg {k}" style="width:{max((b - a) / size * 100, 0.05):.3f}%" '
            f'href="#u-{a:X}" title="{k} 0x{a:X}..0x{b:X} ({b - a} bytes)"></a>'
            if a in dumped else
            f'<span class="seg {k}" style="width:{max((b - a) / size * 100, 0.05):.3f}%" '
            f'title="{k} 0x{a:X}..0x{b:X} ({b - a} bytes)"></span>'
            for a, b, k in runs)
        totals = defaultdict(int)
        for a, b, k in runs:
            totals[k] += b - a
        legend = " ".join(f'<span class="key {k}"></span>{k} {v:,}' for k, v in sorted(totals.items(), key=lambda kv: -kv[1]))
        unclaimed = [(a, b) for a, b, k in runs if k == "unclaimed" and not all(x == 0 for x in self.p.blob.data[a:b])]
        dumps = "".join(
            f'<details id="u-{a:X}"><summary>0x{a:X}..0x{b:X} &middot; {b - a} bytes</summary>'
            f'<pre>{_hexdump(self.p.blob.data, a, min(b, a + 512))}{"…" if b - a > 512 else ""}</pre></details>'
            for a, b in unclaimed[:200])
        return (f'<section id="coverage"><h2>Blob map</h2><div class="bar">{bars}</div>'
                f'<p class="legend">{legend}</p>'
                f'<p class="note">Unclaimed bytes are ones nothing reachable from the header names: data a '
                f'computed offset reaches, dead code, or structure this tool does not know yet. '
                f'All-zero runs are left out of the list below.</p>{dumps}</section>')

    # -- page ---------------------------------------------------------------

    def page(self, fragment: bool = False) -> str:
        p = self.p
        routines = sorted(p.routines)
        fixed = [e for e in routines if any(n in p.routines[e].names for n in ("init", "start", "tick", "interact", "teardown"))]
        others = [e for e in routines if e not in fixed]
        runs = self.coverage()

        header_rows = "".join(
            f"<tr><td class='num'>{i}</td><td>{name}</td><td class='off'>0x{p.blob.header[i]:X}</td><td>{desc}</td></tr>"
            for i, (name, desc) in enumerate(HEADER_WORDS))
        sources = defaultdict(int)
        for instr in p.instrs.values():
            sources[instr.op.source] += 1
        defaults = p.scene_defaults()
        extras = []
        if defaults:
            slots = ", ".join(f"{k}:0x{v:X}" for k, v in enumerate(defaults["slots"]) if v)
            extras.append(f"<li>texture slots (FUN_0022A178): {slots or 'none'}</li>")
            if "spawn" in defaults:
                x, y, z = defaults["spawn"]
                extras.append(f"<li>default spawn ({x:g}, {y:g}, {z:g}), draw distance {defaults['draw_distance']:g}</li>")
        music = p.music_requests()
        if music:
            extras.append("<li>music requests: " + ", ".join(
                f"slot {k} = cat {min(k, 2)} #{v & 0x7FFF}{' (play)' if v & 0x8000 else ''}"
                for k, v in enumerate(music) if v) + "</li>")
        for begin, end, k, desc in p.header_blocks():
            extras.append(f"<li><code>0x{begin:X}</code> {html.escape(desc)}</li>")
        diag = "".join(f"<li>0x{o:X}: {html.escape(m)}</li>" for o, m in p.conflicts) or "<li>none</li>"

        nav = []
        nav.append('<div class="navgroup"><h4>Entry points</h4>' + "".join(
            f'<a href="#c-{e:X}">{p.routine_name(e)}</a>' for e in fixed) + "</div>")
        nav.append(f'<div class="navgroup"><h4>Bodies &amp; subroutines ({len(others)})</h4>' + "".join(
            f'<a href="#c-{e:X}" data-k="{p.routine_name(e)}">{p.routine_name(e)}'
            f'<small>{_kind(p.routines[e].names)}</small></a>' for e in others) + "</div>")
        nav.append(f'<div class="navgroup"><h4>Event streams ({len(p.streams)})</h4>' + "".join(
            f'<a href="#s-{o:X}">stream_{o:04X}<small>{len(s.records)}</small></a>' for o, s in sorted(p.streams.items())) + "</div>")
        nav.append(f'<div class="navgroup"><h4>Dialogue ({len(self.messages)})</h4>' + "".join(
            f'<a href="#m-{o:X}" data-k="{html.escape(m.speaker)} {html.escape(T.plain(m))}">msg_{o:04X}<small>{html.escape(m.speaker)}</small></a>'
            for o, m in sorted(self.messages.items())) + "</div>")
        if self.speculative:
            nav.append(f'<div class="navgroup"><h4>Unreferenced code? ({len(self.speculative)})</h4>' + "".join(
                f'<a href="#c-{e:X}">unref_{e:04X}<small>{self._unref_tag(e, r)}</small></a>'
                for e, r in sorted(self.speculative.items())) + "</div>")
        if p.point_lists:
            nav.append(f'<div class="navgroup"><h4>Paths ({len(p.point_lists)})</h4>' + "".join(
                f'<a href="#p-{o:X}">path_{o:04X}<small>{len(pl.points)}</small></a>' for o, pl in sorted(p.point_lists.items())) + "</div>")

        body = []
        body.append(f'<section id="overview"><h2>{html.escape(self.title)}</h2>'
                    f'<p class="stats">{len(p.blob):,} bytes &middot; {len(p.routines)} routines &middot; '
                    f'{len(p.instrs):,} instructions &middot; {len(p.streams)} event streams &middot; '
                    f'{len(self.messages)} dialogue records &middot; {len(p.conflicts)} decode errors</p>'
                    f'<table class="hdr"><thead><tr><th>word</th><th>name</th><th>value</th><th></th></tr></thead>'
                    f'<tbody>{header_rows}</tbody></table>'
                    f'<p class="note">Operand shapes used: '
                    + ", ".join(f"{k} {v:,}" for k, v in sorted(sources.items()))
                    + ' instructions (port = the native port\'s interpreter, elf/ghidra = read off the retail handler). '
                    'Hover a line for its handler and bytes.</p>'
                    f'<ul class="note">{"".join(extras)}</ul>'
                    f'<details><summary>Decode errors</summary><ul>{diag}</ul></details></section>')
        body.append(self._coverage_section(runs))
        body.append('<h2 id="code">Code</h2>')
        body.extend(self._routine(e) for e in fixed + others)
        if self.speculative:
            body.append('<h2 id="unreferenced">Unreferenced code (speculative)</h2><p class="note">Nothing reachable '
                        'from the header names these bytes, but they decode cleanly to an <code>end</code>. The engine '
                        'may start them from data this tool does not parse &mdash; an entity type&rsquo;s own handler, '
                        'the battle VM &mdash; or they may be dead. Treat every line here as a candidate, not a fact.</p>')
            body.extend(self._routine(e, r) for e, r in sorted(self.speculative.items()))
        body.append('<h2 id="streams">Event streams</h2><p class="note">FUN_0025ce30 pays these out one record '
                    'at a time: wait for the gate, then count <em>wait</em> frames, then start the target &mdash; '
                    'a dialogue record directly, anything else in a free object-script slot.</p>')
        body.extend(self._stream(s) for _, s in sorted(p.streams.items()))
        body.append('<h2 id="dialogue">Dialogue</h2>')
        body.extend(self._message(o, m) for o, m in sorted(self.messages.items()))
        if p.point_lists:
            body.append('<h2 id="paths">Paths</h2><p class="note">Point lists named by camera paths (0x41/0x43), '
                        'path-follow (0xBD method 0x70) and 0x144. Coordinates are world units.</p>')
            body.extend(self._path(pl) for _, pl in sorted(p.point_lists.items()))

        return PAGE.format(title=html.escape(self.title), nav="".join(nav), body="\n".join(body),
                           doc_open="" if fragment else DOC_OPEN, head_close="" if fragment else "</head>\n<body>",
                           doc_close="" if fragment else DOC_CLOSE)


def _kind(names: list[str]) -> str:
    tags = sorted(set(names))
    short = {"sub": "call", "event": "event", "install.body": "install", "make_npc.body": "npc",
             "spawn_npc.body": "npc", "install_lead_script.body": "lead"}
    return " ".join(sorted({short.get(t, t.split(".")[0]) for t in tags}))


def _hexdump(data: bytes, begin: int, end: int) -> str:
    lines = []
    for row in range(begin - begin % 16, end, 16):
        chunk = data[max(row, begin):min(row + 16, end)]
        pad = "   " * (max(row, begin) - row)
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"{row:05X}  {pad}{chunk.hex(' '):<{48 - len(pad)}}  {html.escape(text)}")
    return "\n".join(lines)


# An Artifact publish supplies its own doctype/head/body wrapper.
DOC_OPEN = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
"""
DOC_CLOSE = "</body></html>\n"

PAGE = """{doc_open}<title>{title}</title>
<meta name="claude:viewport-fit-sides" content="cover">
<style>
:root {{
  --bg: #fbfaf7; --panel: #f2f0ea; --text: #23211d; --muted: #6e6a61; --line: #dedad0;
  --accent: #2f5fa7; --code: #1c1b18; --kw: #8a3b8f; --lit: #a0522d; --str: #2e7d4f;
  --c-code: #5b8bd6; --c-text: #5cad7a; --c-stream: #d4a03a; --c-path: #c0607a; --c-table: #8f7fc9;
  --c-header: #888; --c-id: #b9b2a3; --c-unclaimed: #e4dfd3; --err: #b3261e; --hl: #fff1b8;
}}
@media (prefers-color-scheme: dark) {{ :root:not([data-theme="light"]) {{
  --bg: #17171a; --panel: #1f1f23; --text: #e6e3dc; --muted: #9a958b; --line: #34343a;
  --accent: #8ab4f8; --code: #e6e3dc; --kw: #d39ad6; --lit: #e0a072; --str: #8fd3a5;
  --c-unclaimed: #33322e; --c-id: #5d5a52; --hl: #4a4220; --err: #f28b82;
}} }}
:root[data-theme="dark"] {{
  --bg: #17171a; --panel: #1f1f23; --text: #e6e3dc; --muted: #9a958b; --line: #34343a;
  --accent: #8ab4f8; --code: #e6e3dc; --kw: #d39ad6; --lit: #e0a072; --str: #8fd3a5;
  --c-unclaimed: #33322e; --c-id: #5d5a52; --hl: #4a4220; --err: #f28b82;
}}
* {{ box-sizing: border-box; }}
body {{ margin: 0; background: var(--bg); color: var(--text); font: 14px/1.45 system-ui, sans-serif; }}
nav {{ position: fixed; top: env(safe-area-inset-top, 0px); left: 0; bottom: 0; width: 270px; overflow-y: auto;
  background: var(--panel); border-right: 1px solid var(--line);
  padding: 12px 12px 12px max(12px, env(safe-area-inset-left, 0px)); }}
#navtoggle {{ display: none; }}
nav input {{ width: 100%; padding: 6px 8px; border: 1px solid var(--line); border-radius: 6px;
  background: var(--bg); color: var(--text); margin-bottom: 8px; }}
nav h4 {{ margin: 12px 0 4px; font-size: 12px; text-transform: uppercase; letter-spacing: .04em; color: var(--muted); }}
nav a {{ display: flex; justify-content: space-between; gap: 8px; padding: 1px 4px; color: var(--text);
  text-decoration: none; font: 12px/1.6 ui-monospace, Consolas, monospace; border-radius: 4px; }}
nav a:hover {{ background: var(--line); }}
nav small {{ color: var(--muted); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }}
nav .top a {{ display: inline; font-family: system-ui; margin-right: 8px; color: var(--accent); }}
main {{ margin-left: 270px; padding-block: 16px 80px; padding-left: 24px;
  padding-right: max(24px, env(safe-area-inset-right, 0px)); max-width: 1200px; min-width: 0; }}
h2 {{ margin: 32px 0 8px; border-bottom: 1px solid var(--line); padding-bottom: 4px; }}
h3 {{ margin: 0 0 4px; font: 600 14px ui-monospace, Consolas, monospace; display: flex; flex-wrap: wrap;
  gap: 4px 12px; align-items: baseline; }}
.meta {{ font: 12px system-ui; color: var(--muted); }}
section.routine, section.stream, section.msg, section.path {{ border: 1px solid var(--line); border-radius: 8px;
  padding: 10px 12px; margin: 12px 0; background: var(--bg); }}
section:target, tr:target, .ln:target {{ outline: 2px solid var(--accent); }}
.ln:target, tr:target {{ background: var(--hl); }}
.lines {{ font: 12.5px/1.5 ui-monospace, Consolas, monospace; overflow-x: auto; }}
.ln {{ display: flex; white-space: pre; }}
.ln:hover {{ background: var(--panel); }}
.off {{ color: var(--muted); width: 52px; flex: none; text-decoration: none; font: 12px ui-monospace, Consolas, monospace; }}
.code {{ color: var(--code); }}
a.ref {{ color: var(--accent); text-decoration: none; }}
a.ref:hover {{ text-decoration: underline; }}
a.ref.label {{ color: var(--kw); }}
.nolink {{ color: var(--muted); }}
.lit {{ color: var(--lit); }}
.xref {{ font-size: 12px; color: var(--muted); margin: 0 0 6px; }}
.toggle {{ margin-left: auto; font-size: 11px; padding: 1px 8px; border: 1px solid var(--line); border-radius: 4px;
  background: var(--panel); color: var(--muted); cursor: pointer; }}
table {{ border-collapse: collapse; font-size: 13px; display: block; max-width: 100%; overflow-x: auto; }}
td, th {{ border-bottom: 1px solid var(--line); padding: 2px 10px 2px 0; text-align: left; vertical-align: top; }}
th {{ color: var(--muted); font-weight: 500; }}
td.num {{ text-align: right; font-variant-numeric: tabular-nums; }}
.say {{ color: var(--muted); }}
.text {{ line-height: 1.9; }}
.txt {{ color: var(--str); }}
.spk {{ font-weight: 600; margin-right: 6px; }}
.cc {{ font: 11px ui-monospace, Consolas, monospace; color: var(--muted); background: var(--panel);
  border: 1px solid var(--line); border-radius: 4px; padding: 0 4px; margin: 0 3px; }}
.err {{ color: var(--err); font-size: 12px; }}
.note, .stats, .legend {{ color: var(--muted); font-size: 13px; }}
.badge {{ font-size: 12px; font-weight: 600; color: var(--accent); margin-left: 6px; }}
.known {{ margin: 6px 0 8px; padding: 8px 10px; border-left: 3px solid var(--accent); background: var(--panel); font-size: 13px; max-width: 72ch; }}
.bar {{ display: flex; height: 22px; border-radius: 4px; overflow: hidden; border: 1px solid var(--line); }}
.seg {{ display: block; height: 100%; }}
.seg.code, .key.code {{ background: var(--c-code); }}
.seg.text, .key.text {{ background: var(--c-text); }}
.seg.stream, .key.stream {{ background: var(--c-stream); }}
.seg.path, .key.path {{ background: var(--c-path); }}
.seg.table, .key.table {{ background: var(--c-table); }}
.seg.header, .key.header {{ background: var(--c-header); }}
.seg.id, .key.id {{ background: var(--c-id); }}
.seg.unclaimed, .key.unclaimed {{ background: var(--c-unclaimed); }}
.seg.speculative, .key.speculative {{ background: repeating-linear-gradient(45deg, var(--c-code) 0 3px, var(--c-unclaimed) 3px 6px); }}
.seg.defaults, .key.defaults, .seg.music, .key.music, .seg.sections, .key.sections {{ background: var(--c-header); }}
section.speculative {{ border-style: dashed; }}
.key {{ display: inline-block; width: 10px; height: 10px; border-radius: 2px; margin: 0 4px 0 12px; }}
pre {{ font: 12px ui-monospace, Consolas, monospace; overflow-x: auto; }}
.hidden {{ display: none !important; }}
@media (max-width: 800px) {{
  body::before {{ content: ""; position: fixed; inset: 0 0 auto; height: env(safe-area-inset-top, 0px);
    z-index: 10; background: var(--panel); }}
  nav {{ position: sticky; z-index: 5; width: auto; max-height: 75vh; border-right: 0; border-bottom: 1px solid var(--line);
    padding: 8px max(16px, env(safe-area-inset-right, 0px)) 8px max(16px, env(safe-area-inset-left, 0px)); }}
  #navtoggle {{ display: inline-block; float: right; padding: 4px 10px; border: 1px solid var(--line); border-radius: 6px;
    background: var(--bg); color: var(--text); font: 13px system-ui, sans-serif; }}
  nav .top a {{ line-height: 2; }}
  nav:not(.open) input, nav:not(.open) .navgroup {{ display: none; }}
  main {{ margin-left: 0; padding-block: 12px 80px; padding-left: max(16px, env(safe-area-inset-left, 0px));
    padding-right: max(16px, env(safe-area-inset-right, 0px)); }}
  [id] {{ scroll-margin-top: 56px; }}
}}
</style>{head_close}
<nav id="nav">
  <button id="navtoggle" type="button" aria-expanded="false">Contents</button>
  <input id="q" type="search" placeholder="Filter: name, speaker, text…" autocomplete="off">
  <div class="top"><a href="#overview">Overview</a><a href="#coverage">Map</a><a href="#code">Code</a>
  <a href="#streams">Streams</a><a href="#dialogue">Dialogue</a></div>
  {nav}
</nav>
<main>
{body}
</main>
<script>
document.querySelectorAll('.toggle').forEach(function (b) {{
  b.addEventListener('click', function () {{
    var s = b.closest('section'), st = s.querySelector('.structured'), fl = s.querySelector('.flat');
    var flat = st.hidden; st.hidden = !flat; fl.hidden = flat; b.textContent = flat ? 'flat' : 'structured';
  }});
}});
var nav = document.getElementById('nav'), navToggle = document.getElementById('navtoggle');
navToggle.addEventListener('click', function () {{
  var open = nav.classList.toggle('open'); navToggle.setAttribute('aria-expanded', open);
  navToggle.textContent = open ? 'Close' : 'Contents';
}});
nav.addEventListener('click', function (e) {{
  if (e.target.closest('a') && nav.classList.contains('open')) {{ navToggle.click(); }}
}});
var q = document.getElementById('q');
q.addEventListener('input', function () {{
  var t = q.value.toLowerCase();
  document.querySelectorAll('nav .navgroup a').forEach(function (a) {{
    var k = (a.textContent + ' ' + (a.dataset.k || '')).toLowerCase();
    a.classList.toggle('hidden', t && k.indexOf(t) < 0);
  }});
}});
</script>
{doc_close}"""
