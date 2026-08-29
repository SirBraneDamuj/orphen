#!/usr/bin/env python3
"""Print the entity pool out of a PS2 EE memory dump.

The output is column-for-column identical to the `entities (offsets are into
the original's 0x1D8 record)` block the native port writes when you press `G`
(`PortRuntime::writeDiagnosticSnapshot`), so the two can be compared with a
plain `diff`:

    python scripts/dump_ee_entities.py <dump> > hw.txt
    diff hw.txt <(sed -n '/^entities /,/^scheduler:/p' orphen_snapshot_1234.txt)

`<dump>` is either an `eeMemory.bin` or the PCSX2 save-state directory holding
one. PS2 addresses map to file offsets 1:1, so every read below is at the
address the decompilation names.

Two columns need a caveat, the same one the port's comment carries. `+6C` and
`+70` are the ground query's winning / ANDed terrain flags after
FUN_00227070, but other systems write them too, so they only agree for an
entity whose last write came from the query. `+0A` is `primitive | (half << 14)`
and is the sharpest field in the row: if it matches, the scan walked the same
cell run to the same place.
"""

import argparse
import os
import struct
import sys

# FUN_00266240's pool. Stride and the status array are from the decompilation.
POOL_BASE = 0x0058BEB0
POOL_STRIDE = 0x1D8
SLOT_COUNT = 128

HEADER = (
    "slot  +00    +02    +04    +08  +0C        +0A     "
    "pos                              +4C      +54    +58    +5C      "
    "+60 +62  +64  +6C        +70        +74        +A0  +192"
)


def resolve(path):
    if os.path.isdir(path):
        candidate = os.path.join(path, "eeMemory.bin")
        if not os.path.isfile(candidate):
            raise SystemExit("no eeMemory.bin in %s" % path)
        return candidate
    return path


# The columns of one printed row, in order, as (label, width-in-characters). A
# row is fixed-width, so the port's line can be sliced apart without reparsing
# its formatting -- which is the point of the two sides sharing one layout.
FIELDS = [
    ("+00", 7), ("+02", 7), ("+04", 7), ("+08", 7), ("+0C", 11), ("+0A", 7),
    ("pos", 32), ("+4C", 9), ("+54", 7), ("+58", 7), ("+5C", 9), ("+60", 4),
    ("+62", 4), ("+64", 5), ("+6C", 11), ("+70", 11), ("+74", 11), ("+A0", 5),
    ("+192", 5),
]


def split_row(line):
    """Slice a fixed-width row into (label, text) pairs, skipping the slot."""
    pieces = []
    cursor = 4
    for label, width in FIELDS:
        pieces.append((label, line[cursor:cursor + width].strip()))
        cursor += width
    return pieces


def compare(snapshot_path, rows, u16, s16, u32, s32, f32):
    """Report per-field divergence between this dump and a port snapshot."""
    port = {}
    with open(snapshot_path, encoding="utf-8", errors="replace") as handle:
        inside = False
        for line in handle:
            line = line.rstrip(chr(10))
            if line.startswith("entities (offsets"):
                inside = True
                continue
            if not inside:
                continue
            if not line or line.startswith("slot "):
                continue
            if not line[:4].strip().isdigit():
                break
            port[int(line[:4])] = line

    only_hw = sorted(set(rows) - set(port))
    only_port = sorted(set(port) - set(rows))
    if only_hw:
        print("occupied in the dump but not the port: %s" % only_hw)
    if only_port:
        print("occupied in the port but not the dump: %s" % only_port)

    differing = 0
    for slot in sorted(set(rows) & set(port)):
        base = rows[slot]
        mine = (
            "%4d 0x%04x 0x%04x 0x%04x 0x%04x 0x%08x%7d (%9.4f,%9.4f,%9.4f)%9.4f"
            "%7.4f%7.4f%9.4f%4d%4d%5d 0x%08x 0x%08x 0x%08x%5d%5d"
            % (slot, u16(base + 0x00), u16(base + 0x02), u16(base + 0x04),
               u16(base + 0x08), u32(base + 0x0C), s16(base + 0x0A),
               f32(base + 0x20), f32(base + 0x24), f32(base + 0x28), f32(base + 0x4C),
               f32(base + 0x54), f32(base + 0x58), f32(base + 0x5C), u16(base + 0x60),
               u16(base + 0x62), s32(base + 0x64), u32(base + 0x6C), u32(base + 0x70),
               u32(base + 0x74), u16(base + 0xA0), s16(base + 0x192)))
        bad = [(label, hw, pt)
               for (label, hw), (_, pt) in zip(split_row(mine), split_row(port[slot]))
               if hw != pt]
        if not bad:
            continue
        differing += 1
        print("slot %3d  %s" % (slot, "  ".join(
            "%s hw=%s port=%s" % (label, hw, pt) for label, hw, pt in bad)))

    print("%d of %d shared slots differ in at least one field"
          % (differing, len(set(rows) & set(port))))

    # Which columns go wrong most often is the useful summary: one field wrong
    # everywhere is one bug, twenty fields wrong on one slot is another.
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dump", help="eeMemory.bin, or a save-state directory holding one")
    parser.add_argument("--slots", help="comma-separated slots to print instead of all occupied")
    parser.add_argument("--compare", metavar="SNAPSHOT",
                        help="an orphen_snapshot_*.txt from the port. Instead of printing the "
                             "table, align the two by slot and report only the fields that "
                             "differ, which is what a whole-block diff cannot do.")
    arguments = parser.parse_args()

    with open(resolve(arguments.dump), "rb") as handle:
        data = handle.read()

    def u16(address):
        return struct.unpack_from("<H", data, address)[0]

    def s16(address):
        return struct.unpack_from("<h", data, address)[0]

    def u32(address):
        return struct.unpack_from("<I", data, address)[0]

    def s32(address):
        return struct.unpack_from("<i", data, address)[0]

    def f32(address):
        return struct.unpack_from("<f", data, address)[0]

    wanted = None
    if arguments.slots:
        wanted = {int(piece, 0) for piece in arguments.slots.split(",")}

    rows = {}
    for slot in range(SLOT_COUNT):
        base = POOL_BASE + slot * POOL_STRIDE
        if base + POOL_STRIDE > len(data):
            break
        if u16(base + 0x00) == 0 and u16(base + 0x08) == 0 and u16(base + 0x04) == 0:
            continue
        rows[slot] = base

    if arguments.compare:
        return compare(arguments.compare, rows, u16, s16, u32, s32, f32)

    print("entities (offsets are into the original's 0x1D8 record):")
    print(HEADER)
    for slot in range(SLOT_COUNT):
        base = POOL_BASE + slot * POOL_STRIDE
        if base + POOL_STRIDE > len(data):
            break
        type_id = u16(base + 0x00)
        flags04 = u16(base + 0x04)
        flags08 = u16(base + 0x08)
        if wanted is None:
            # The port's own skip: an untouched pool record reads zero here.
            if type_id == 0 and flags08 == 0 and flags04 == 0:
                continue
        elif slot not in wanted:
            continue

        print(
            "%4d 0x%04x 0x%04x 0x%04x 0x%04x 0x%08x%7d (%9.4f,%9.4f,%9.4f)%9.4f"
            "%7.4f%7.4f%9.4f%4d%4d%5d 0x%08x 0x%08x 0x%08x%5d%5d"
            % (
                slot,
                type_id,
                u16(base + 0x02),
                flags04,
                flags08,
                u32(base + 0x0C),
                s16(base + 0x0A),
                f32(base + 0x20),
                f32(base + 0x24),
                f32(base + 0x28),
                f32(base + 0x4C),
                f32(base + 0x54),
                f32(base + 0x58),
                f32(base + 0x5C),
                u16(base + 0x60),
                u16(base + 0x62),
                s32(base + 0x64),
                u32(base + 0x6C),
                u32(base + 0x70),
                u32(base + 0x74),
                u16(base + 0xA0),
                s16(base + 0x192),
            )
        )

    # The two values that date a dump exactly, printed in the same shape the
    # port's snapshot uses. The scheduler cursor is an absolute pointer here;
    # subtract the script base at iGpffffb0e8 (0x355058) to get a blob offset.
    script_base = u32(0x00355058)
    pieces = []
    for channel in range(4):
        record = 0x00571E40 + channel * 12
        cursor = u32(record)
        offset = cursor - script_base if cursor and script_base else 0
        pieces.append("  %d{cursor=0x%x timer=%d consumed=%d}"
                      % (channel, offset & 0xFFFFFFFF, u32(record + 4), u32(record + 8)))
    print("scheduler:" + "".join(pieces))
    print("fade: in=0x%x out=0x%x" % (u16(0x00571DC0), u16(0x00571DD0)))


if __name__ == "__main__":
    sys.exit(main())
