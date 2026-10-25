"""Check opcodes.py's operand shapes against the retail handlers.

Reads both dispatch tables out of SLUS_200.11 (PTR_LAB_0031e228, standard,
and PTR_LAB_0031e538, extended), then scans each handler's MIPS for its
stream reads: `jal 0x25c258` (an expression), `jal 0x25c1d0` (a u32), and
loads through the stream pointer at -0x42A0(gp) (inline bytes). Callees are
followed two levels deep.

The scan is linear and does not follow branches, so a handler that reads
conditionally or in a loop is counted wrong. Those are listed in BRANCHY with
where their shape came from instead; anything else that disagrees is a bug in
the table.

    python -m tools.scrdis.elfscan [--disc-root disc] [--dump]
"""
from __future__ import annotations

import argparse
import bisect
import os
import re
import struct
from pathlib import Path

from .opcodes import OPS

STANDARD_TABLE = 0x31E228
EXTENDED_TABLE = 0x31E538
EXPR, U32, JUMP = 0x25C258, 0x25C1D0, 0x25C220
STREAM_GP = -0x42A0   # DAT_00355CD0 / pbGpffffbd60, the one stream pointer

# Handlers whose reads branch on the opcode, loop, or happen inside a helper,
# and where their shape was taken from instead.
BRANCHY = {
    0x41: "port: two list offsets and a flags byte; the lists are read with the cursor moved",
    0x43: "port: three list offsets; lists read with the cursor moved",
    0x46: "port: six expressions in a loop",
    0x47: "port: three expressions in a loop",
    0x48: "port: three expressions in a loop",
    0x4D: "port: count byte, then count u32s",
    0x51: "port: one group byte; the other loads are not the stream",
    0x54: "port: index then x, y, z in a loop",
    0x55: "port: index then x, y, z in a loop",
    0x5B: "ghidra: expression then one byte; the second load is a table, not the stream",
    0x63: None,
    0xA5: "port: expression, byte, expression; the tail is another handler",
    0xE7: "port: 0xE7 reads nothing, 0xE8 one expression",
    0xEF: "port: speed, x, y",
    0xF0: "port: x, y",
    0xF1: "port: x, y",
    0x125: "port: u16 cue, selector; 0x126 adds a volume",
    0x127: "port: u16 cue, x, y, z; 0x128 adds a volume",
    0x140: "port: 0x141 adds a bone",
    0x144: "ghidra: two expressions; FUN_0025d618 reads the point list with the cursor moved",
}
for _base, _extra in ((0x116, 3), (0x119, 3), (0x11C, 4), (0x11F, 2), (0x122, 4)):
    for _code in range(_base, _base + 3):
        BRANCHY[_code] = "ghidra: the first of each triple takes an extra entity selector"
del BRANCHY[0x63]


class Elf:
    def __init__(self, path: Path):
        self.data = path.read_bytes()
        phoff = struct.unpack_from("<I", self.data, 0x1C)[0]
        phnum = struct.unpack_from("<H", self.data, 0x2C)[0]
        self.segments = []
        for i in range(phnum):
            _, offset, vaddr, _, filesz, _ = struct.unpack_from("<IIIIII", self.data, phoff + i * 32)
            self.segments.append((vaddr, offset, filesz))

    def word(self, va: int) -> int | None:
        for vaddr, offset, size in self.segments:
            if vaddr <= va < vaddr + size:
                return struct.unpack_from("<I", self.data, offset + va - vaddr)[0]
        return None


def handlers(elf: Elf) -> dict[int, int]:
    out = {}
    for i in range(0xFF - 0x32):
        out[0x32 + i] = elf.word(STANDARD_TABLE + i * 4)
    for i in range(0x50):
        address = elf.word(EXTENDED_TABLE + i * 4)
        if address:
            out[0x100 + i] = address
    return out


def scan(elf: Elf, starts: list[int], address: int, depth: int = 0) -> list[str]:
    i = bisect.bisect_right(starts, address)
    end = starts[i] if i < len(starts) else address + 0x400
    events, cursor_regs = [], set()
    pc = address
    while pc < end:
        w = elf.word(pc)
        op, rs, rt = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        imm = (w & 0xFFFF) - 0x10000 if w & 0x8000 else w & 0xFFFF
        if op in (2, 3):
            target = ((w & 0x3FFFFFF) << 2) | (pc & 0xF0000000)
            if target == EXPR:
                events.append("E")
            elif target == U32:
                events.append("W")
            elif target == JUMP:
                events.append("J")
            elif depth < 2:
                events.extend(scan(elf, starts, target, depth + 1))
        elif op == 0x23 and rs == 28 and imm == STREAM_GP:
            cursor_regs.add(rt)
        elif op in (0x20, 0x24) and rs in cursor_regs and imm >= 0:
            events.append("B")
        elif op == 0x09 and rs in cursor_regs:
            cursor_regs.add(rt)
        pc += 4
    return events


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--disc-root", default="disc")
    ap.add_argument("--src", default="src", help="Ghidra export, used only for function boundaries")
    ap.add_argument("--dump", action="store_true", help="print every handler's scanned reads")
    args = ap.parse_args(argv)
    elf = Elf(Path(args.disc_root) / "SLUS_200.11")
    starts = sorted(int(m.group(1), 16) for f in os.listdir(args.src)
                    if (m := re.match(r"FUN_([0-9a-f]{8})\.c$", f)))
    table = handlers(elf)
    bad = 0
    for code, address in sorted(table.items()):
        op = OPS.get(code)
        if op is None:
            print(f"0x{code:X}: handler 0x{address:08X} has no entry in opcodes.py")
            bad += 1
            continue
        if op.handler and op.handler != address:
            print(f"0x{code:X} {op.name}: table says handler 0x{op.handler:X}, ELF says 0x{address:X}")
            bad += 1
        events = scan(elf, starts, address)
        got = (events.count("E"), events.count("W"), events.count("B"))
        want = (sum(o.kind == "e" for o in op.operands), sum(o.kind == "w" for o in op.operands),
                sum(o.kind == "b" for o in op.operands) + 2 * sum(o.kind == "h" for o in op.operands))
        if args.dump:
            print(f"0x{code:X} {op.name:30} {' '.join(events)}")
        if code in (0x33, 0x3D, 0x3E, 0x3F, 0x40) or 0xF6 <= code <= 0xFE:
            continue   # 0x33 is decoded specially; 0x3D..0x40 re-read their own opcode byte
        if got != want and code not in BRANCHY:
            print(f"0x{code:X} {op.name}: ELF reads E{got[0]} W{got[1]} B{got[2]}, "
                  f"table has E{want[0]} W{want[1]} B{want[2]}  [{op.source}]")
            bad += 1
    print(f"{len(table)} handlers checked, {len(BRANCHY)} taken from port/ghidra, {bad} disagreements")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
