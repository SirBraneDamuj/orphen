"""Read a flat TOC-style `.BIN` file (GRP / SCR / MAP / TEX / ITM / SND / VOICE).

Layout (from FUN_00223268 + FUN_00221b18 + baseline/extract_map_bin.py):

    u32 entry_count
    u32 entries[entry_count]
        bits [ 0:17] = size in 32-bit words (so bytes = words * 4)
        bits [17:32] = sector offset (sector = 2048 bytes)

Each entry's payload is usually headerless-LZ compressed (decoder at
src/FUN_002f3118.c; Python port at baseline/lz_decoder.py).  A few entries are
empty (size == 0).

No knowledge of per-entry format is baked in here — decompression + magic
dispatch happens in `extract_all.py`.
"""
from __future__ import annotations

import os
import struct
from dataclasses import dataclass
from typing import List, Tuple

SECTOR = 2048


@dataclass
class Entry:
    index: int
    sector: int
    byte_offset: int
    size: int


def parse_toc(path: str) -> Tuple[bytes, List[Entry]]:
    with open(path, "rb") as f:
        data = f.read()
    (count,) = struct.unpack_from("<I", data, 0)
    entries: List[Entry] = []
    for i in range(count):
        (raw,) = struct.unpack_from("<I", data, 4 + i * 4)
        words = raw & 0x1FFFF
        sector = (raw >> 17) & 0x7FFF
        entries.append(Entry(i, sector, sector * SECTOR, words * 4))
    return data, entries


def read_entry(buf: bytes, entry: Entry) -> bytes:
    if entry.size == 0:
        return b""
    return buf[entry.byte_offset : entry.byte_offset + entry.size]


def iter_entries(path: str):
    """Yield (Entry, raw_bytes) for each non-empty entry in a BIN."""
    data, entries = parse_toc(path)
    for e in entries:
        if e.size == 0:
            continue
        yield e, read_entry(data, e)


def summarize(path: str) -> None:
    data, entries = parse_toc(path)
    non_empty = [e for e in entries if e.size > 0]
    total = sum(e.size for e in non_empty)
    print(f"{os.path.basename(path)}: {len(entries)} entries "
          f"({len(non_empty)} non-empty), {total:,} bytes packed")


if __name__ == "__main__":
    import sys
    for p in sys.argv[1:]:
        summarize(p)
