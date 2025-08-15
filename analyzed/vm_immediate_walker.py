"""
VM Immediate Walker (source-validated)

Implements a minimal decoder for the VM-side immediate fetches as per FUN_0025bf70.
Use this to inspect/annotate the 4-byte payload after structural 0x32.

Scope:
- 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11: decoded exactly.
- 0x30, 0x31: marked as composite immediate sites (requires nested VM eval); not decoded here.

Notes:
- This does NOT execute the VM (FUN_0025c258); it only mirrors how the VM pulls
  structural immediates from pbGpffffbd60.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import List, Tuple, Optional


@dataclass
class Item:
    off: int
    op: int
    desc: str
    size: int
    value: Optional[int] = None


class StructuralStream:
    def __init__(self, buf: bytes, start: int):
        self.buf = buf
        self.pc = start

    def eof(self) -> bool:
        return self.pc >= len(self.buf)

    def peek(self) -> int:
        return self.buf[self.pc]

    def read(self, n: int) -> bytes:
        b = self.buf[self.pc : self.pc + n]
        self.pc += n
        return b


def _i16(b: bytes) -> int:
    x = int.from_bytes(b, "little", signed=True)
    return x


def _u16(b: bytes) -> int:
    return int.from_bytes(b, "little", signed=False)


def _i32(b: bytes) -> int:
    return int.from_bytes(b, "little", signed=True)


def _u32(b: bytes) -> int:
    return int.from_bytes(b, "little", signed=False)


def walk_vm_immediates(buf: bytes, start: int, max_items: int = 64) -> List[Item]:
    s = StructuralStream(buf, start)
    out: List[Item] = []
    for _ in range(max_items):
        if s.eof():
            break
        off = s.pc
        op = s.peek()
        # Mirror FUN_0025bf70 switch
        if op == 0x0C:
            s.read(1)
            v = s.read(1)[0]
            out.append(Item(off, op, "IMM8", 2, v))
        elif op == 0x0D:
            b = s.read(3)
            v = b[1] | (b[2] << 8)
            out.append(Item(off, op, "IMM16", 3, v))
        elif op == 0x0E:
            b = s.read(5)
            v = _u32(b[1:])
            out.append(Item(off, op, "IMM32", 5, v))
        elif op == 0x0F:
            s.read(1)
            v = _i32(s.read(4)) * 100
            out.append(Item(off, op, "SCALED_I32_x100", 5, v))
        elif op == 0x10:
            s.read(1)
            v = _i16(s.read(2)) * 1000
            out.append(Item(off, op, "SCALED_I16_x1000", 3, v))
        elif op == 0x11:
            s.read(1)
            v = (_i16(s.read(2)) * 0xF570) // 0x168
            out.append(Item(off, op, "SCALED_I16_deg_to_units", 3, v))
        elif op in (0x30, 0x31):
            # Composite immediate via nested VM eval: FUN_0025c258 calls.
            # We don’t emulate VM here—just mark it.
            s.read(1)
            out.append(Item(off, op, "COMPOSITE_IMM(undecoded)", 1, None))
        else:
            # default: return 0 (stop). We don’t advance pc.
            out.append(Item(off, op, "STOP(non-immediate)", 0, None))
            break
    return out


def main(argv: List[str]) -> None:
    import argparse
    import pathlib

    ap = argparse.ArgumentParser(description="Walk VM immediates starting at offset")
    ap.add_argument("file", nargs="?", default="scr2.out", help="Binary containing structural/VM stream")
    ap.add_argument("--start", type=lambda x: int(x, 0), required=True, help="Offset to start reading (typically after 0x32)")
    ap.add_argument("--limit", type=int, default=64)
    args = ap.parse_args(argv)

    p = pathlib.Path(args.file)
    data = p.read_bytes()
    items = walk_vm_immediates(data, args.start, args.limit)
    for it in items:
        val = f" value={it.value}" if it.value is not None else ""
        print(f"{it.off:08x}: op={it.op:02x} {it.desc}{val}")


if __name__ == "__main__":
    import sys
    main(sys.argv[1:])
