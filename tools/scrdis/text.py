"""Dialogue records: the bytes FUN_00237de8 walks.

Printable bytes are glyphs; anything below 0x20 is a control code whose width
is FUN_00237de8_controlWidth in port/src/ported/text/original_dialogue_window.cpp
(read off each handler's cursor stores in the ELF). A few codes are special:

  0x00  end of the record (or return, inside a 0x10 call)
  0x10  call: u32 displacement from the byte after the opcode
  0x13  speaker name: glyphs up to a 0x00
  0x15  choice: [work][initial][count], then `count` NUL-terminated options
"""
from __future__ import annotations

from dataclasses import dataclass, field

WIDTH = {
    0x0A: 2, 0x0B: 2, 0x0C: 2, 0x0D: 2, 0x0E: 2, 0x12: 2, 0x14: 2, 0x18: 2, 0x1D: 2,
    0x19: 3, 0x1B: 3, 0x1C: 3, 0x1E: 3,
    0x0F: 4, 0x15: 4,
    0x11: 7, 0x16: 7,
}

NAMES = {
    0x01: "prompt", 0x02: "close", 0x07: "newline", 0x0C: "pause",
    0x16: "voice_cache", 0x18: "voice_play", 0x1A: "voice_wait",
    0x1B: "set_flag",
}


@dataclass
class Piece:
    kind: str             # text, code, speaker, choice, call
    text: str = ""
    code: int = 0
    operands: bytes = b""
    options: list[str] = field(default_factory=list)
    target: int = 0


@dataclass
class Message:
    offset: int
    end: int
    pieces: list[Piece]
    speaker: str = ""
    error: str = ""


def _glyphs(data: bytes, at: int, limit: int) -> tuple[str, int]:
    out = []
    while at < limit and data[at] != 0:
        b = data[at]
        out.append(chr(b) if 0x20 <= b < 0x7F else f"\\x{b:02X}")
        at += 1
    return "".join(out), at + 1


def parse(data: bytes, offset: int, limit: int | None = None) -> Message:
    limit = min(limit or len(data), len(data))
    pieces: list[Piece] = []
    speaker = ""
    at = offset
    run: list[str] = []

    def flush():
        if run:
            pieces.append(Piece("text", "".join(run)))
            run.clear()

    while at < limit:
        b = data[at]
        if b >= 0x20:
            run.append(chr(b) if b < 0x7F else f"\\x{b:02X}")
            at += 1
            continue
        flush()
        if b == 0x00:
            at += 1
            break
        if b == 0x13:
            name, at = _glyphs(data, at + 1, limit)
            speaker = speaker or name
            pieces.append(Piece("speaker", name, b))
            continue
        if b == 0x15:
            head = data[at + 1:at + 4]
            at += 4
            options = []
            for _ in range(head[2] if len(head) == 3 else 0):
                text, at = _glyphs(data, at, limit)
                options.append(text)
            pieces.append(Piece("choice", code=b, operands=head, options=options))
            continue
        if b == 0x10:
            disp = int.from_bytes(data[at + 1:at + 5], "little", signed=True)
            pieces.append(Piece("call", code=b, target=at + 1 + disp))
            at += 5
            continue
        width = WIDTH.get(b, 1)
        pieces.append(Piece("code", code=b, operands=data[at + 1:at + width]))
        at += width
    flush()
    return Message(offset, at, pieces, speaker)


def plain(message: Message) -> str:
    """The words only, with newlines, for a one-line summary."""
    out = []
    for piece in message.pieces:
        if piece.kind == "text":
            out.append(piece.text)
        elif piece.kind == "code" and piece.code == 0x07:
            out.append(" / ")
        elif piece.kind == "choice":
            out.append(" [" + " | ".join(piece.options) + "]")
    return "".join(out).strip()


def code_text(piece: Piece) -> str:
    name = NAMES.get(piece.code, f"{piece.code:02X}")
    if piece.code == 0x1B and len(piece.operands) == 2:
        return f"{{set_flag 0x{int.from_bytes(piece.operands, 'little'):X}}}"
    if piece.code == 0x16 and len(piece.operands) == 6:
        channel, wait = piece.operands[0], piece.operands[1]
        clip = int.from_bytes(piece.operands[2:], "little")
        return f"{{voice_cache ch{channel} 0x{clip:X} wait={wait}}}"
    if piece.code == 0x18 and piece.operands:
        return f"{{voice_play ch{piece.operands[0]}}}"
    if piece.code == 0x0C and piece.operands:
        return f"{{pause {piece.operands[0]}}}"
    if piece.operands:
        return "{" + name + " " + piece.operands.hex(" ") + "}"
    return "{" + name + "}"


def render_inline(program, begin: int, end: int, namer) -> str:
    message = parse(program.blob.data, begin, end)
    return '"' + namer.esc(plain(message)) + '"'
