"""Decode every scene's script and report decode errors -- the shape table's test."""
from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path

from .container import Disc, scene_name
from .decode import Program


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--disc-root", default="disc")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)
    disc = Disc(Path(args.disc_root))
    totals = Counter()
    errors = Counter()
    opcodes = Counter()
    for section, entry in disc.scenes():
        data = disc.script(section, entry)
        if data is None or len(data) < 44:
            if data is not None:
                totals["tiny blobs skipped"] += 1
            continue
        program = Program(data)
        totals["scenes"] += 1
        totals["routines"] += len(program.routines)
        totals["instructions"] += len(program.instrs)
        totals["streams"] += len(program.streams)
        for instr in program.instrs.values():
            opcodes[instr.code] += 1
        if program.conflicts:
            totals["scenes with errors"] += 1
        for offset, message in program.conflicts:
            key = message.split(" at 0x")[0] if "middle" in message else message
            errors[key] += 1
            if args.verbose:
                print(f"{scene_name(section, entry)} 0x{offset:X}: {message}")
    for key, value in totals.items():
        print(f"{key:20} {value}")
    print(f"distinct opcodes     {len(opcodes)}")
    for message, count in errors.most_common(40):
        print(f"  {count:5}  {message}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
