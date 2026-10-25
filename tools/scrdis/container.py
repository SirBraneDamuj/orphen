"""Find a scene's script blob on the disc.

A scene `sNN_eMMM` is MCB0.BIN section NN, entry MMM: an (offset, size) pair
into MCB1.BIN (FUN_00222638 / FUN_00222898). That slice is a bundle of
`[u32 id][u32 size][payload]` records ending at id 0xFFFFFFFF
(FUN_00222c08), and id >> 16 is the archive category. The scene script is the
first category-1 record, LZ-compressed like everything else (FUN_002f3118).
This is the same lookup the port does in PortRuntime (`findFirst(1)`).
"""
from __future__ import annotations

import re
import struct
from pathlib import Path

from ..resource_extract.baseline.lz_decoder import decode_bytes
from ..resource_extract.v2.mcb import load_mcb0
from ..resource_extract.v2.mcb_bundle import iter_records

SCRIPT_CATEGORY = 1


def parse_scene_name(name: str) -> tuple[int, int]:
    m = re.fullmatch(r"s(\d\d)_e(\d\d\d)", name)
    if not m:
        raise ValueError(f"scene must look like s01_e012, got {name!r}")
    return int(m.group(1)), int(m.group(2))


def scene_name(section: int, entry: int) -> str:
    return f"s{section:02d}_e{entry:03d}"


class Disc:
    def __init__(self, root: Path):
        self.root = Path(root)
        self.slots = load_mcb0(str(self.root / "MCB0.BIN"))
        self._mcb1 = self.root / "MCB1.BIN"

    def bundle(self, section: int, entry: int) -> bytes | None:
        offset, size = self.slots[section][entry]
        if size == 0:
            return None
        with open(self._mcb1, "rb") as fh:
            fh.seek(offset)
            return fh.read(size)

    def scenes(self):
        """Every (section, entry) whose bundle carries a script record."""
        for section, row in enumerate(self.slots):
            for entry, (_, size) in enumerate(row):
                if size:
                    yield section, entry

    def script(self, section: int, entry: int) -> bytes | None:
        bundle = self.bundle(section, entry)
        if bundle is None:
            return None
        for _id, category, _rid, _off, payload in iter_records(bundle):
            if category == SCRIPT_CATEGORY:
                return decode_bytes(payload)
        return None
