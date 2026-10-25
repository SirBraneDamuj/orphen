"""Find where the US scripts disabled the battle logo, and print the port's tables.

The JP scripts call 0xDF (battle_logo_show) 120 times. The US ones keep 13 of
those inside `if (0 == 1)` and deleted 85. This finds both from the US disc
alone, for PortRuntime::restoreDisabledBattleLogo:

- **flips**: every `if (0 == 1)` whose body calls 0xDF. The port turns the
  `==` into `!=`.
- **intro switches**: the battle-intro `switch (work[N])` whose cases 10, 40
  and 50 open the way the US versions do once JP's logo lines are gone --
  case 10 with `if (flag(0x81F))`, case 40 with `work[N] = 50`, case 50 with
  `player_lock(1)`. The port points those three case-table entries at JP's
  lines appended to the blob. The only other deleted call, s14_e004's event
  body that is now a bare `retire(-1)`, has no US-side shape to find and is a
  one-off in the port.

    python -m tools.scrdis.battle_logo
"""
from __future__ import annotations

import argparse
from pathlib import Path

from .container import Disc, scene_name
from .decode import Program

DISABLED_IF = bytes.fromhex("010e000000000c01120b")   # if (0 == 1)
IF_FLAG_81F = bytes.fromhex("013d0e1f0800000b0b")     # if (flag(0x81F))
PLAYER_LOCK_1 = bytes.fromhex("6d01")                 # player_lock(1)
INTRO_KEYS = [0, 10, 20, 30, 40, 50, 60]


def _bytes(program: Program, offset: int) -> bytes:
    instr = program.instrs.get(offset)
    return bytes(program.blob.data[instr.offset:instr.end]) if instr else b""


def _is_work_assign_50(code: bytes) -> bool:
    # work[N] = 50:  37 0E <u32 N> 0B 0C 32 0B
    return len(code) >= 10 and code[0] == 0x37 and code[1] == 0x0E and code[6:10] == bytes.fromhex("0b0c320b")


def flips(program: Program) -> list[int]:
    found = []
    for instr in program.instrs.values():
        if instr.code != 0x01 or bytes(program.blob.data[instr.offset:instr.end - 4]) != DISABLED_IF:
            continue
        end = instr.targets[0][1]
        if any(o > instr.offset and o < end and i.code == 0xDF for o, i in program.instrs.items()):
            found.append(instr.offset)
    return sorted(found)


def intro_switches(program: Program) -> list[int]:
    """The offset of each matching switch's first (key, rel32) pair."""
    found = []
    for instr in program.instrs.values():
        if instr.code != 0x02 or [k for k, _ in instr.extra["cases"]] != INTRO_KEYS:
            continue
        cases = dict(instr.extra["cases"])
        if (_bytes(program, cases[10]).startswith(IF_FLAG_81F)
                and _is_work_assign_50(_bytes(program, cases[40]))
                and _bytes(program, cases[50]) == PLAYER_LOCK_1):
            found.append(instr.end - 4 - 8 * len(INTRO_KEYS))
    return sorted(found)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--disc-root", default="disc")
    args = ap.parse_args(argv)
    disc = Disc(Path(args.disc_root))
    flip_rows, switch_rows = [], []
    for section, entry in disc.scenes():
        data = disc.script(section, entry)
        if data is None or len(data) < 44:
            continue
        program = Program(data)
        for offset in flips(program):
            flip_rows.append((section, entry, offset))
        for offset in intro_switches(program):
            switch_rows.append((section, entry, offset))
    sections = {s for s, _, _ in flip_rows + switch_rows}
    print(f"// {len(flip_rows)} flips, {len(switch_rows)} intro switches, "
          f"in section(s) {', '.join(str(s) for s in sorted(sections))}")
    for title, rows in (("flips", flip_rows), ("intro switch case tables", switch_rows)):
        print(f"// {title}")
        cells = [f"{{{entry}, 0x{offset:04X}}}," for _, entry, offset in rows]
        for k in range(0, len(cells), 5):
            print("    " + " ".join(f"{c:14}" for c in cells[k:k + 5]).rstrip())
    for section, entry, offset in flip_rows + switch_rows:
        if section != 14:
            print(f"// note: {scene_name(section, entry)} 0x{offset:X} is outside section 14")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
