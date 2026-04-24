"""Extract every resource record from an MCB1 scene bundle.

Format (derived from `src/FUN_00222c08.c` + `FUN_00222c50.c`):

    Each bundle is a packed linked list of records placed contiguously from
    offset 0, terminated by a sentinel record where `id == 0xFFFFFFFF`:

        u32 id         // (category << 16) | resource_id_within_that_bin
        u32 size       // payload byte count
        u8  payload[size]

    The loader (FUN_00222c08) walks this list searching for a given id;
    FUN_00222c50 appends new records after the existing list.

    The 16-bit "category" matches the dispatch in `FUN_00223268`:
        0 = GRP.BIN    1 = SCR.BIN    2 = MAP.BIN    3 = TEX.BIN
        4 = ITM.BIN                   6 = SND.BIN

    Each record's payload is byte-identical to the corresponding entry of
    the top-level BIN (i.e. still LZ-compressed with the same headerless LZ
    used everywhere else).  We LZ-decode before writing out.

Usage:
    python -m tools.resource_extract.v2.mcb_bundle \
        --src out/all/mcb --dst out/all/mcb_unpacked

Produces a directory tree:
    <dst>/<bundle_stem>/<cat>_<id>.<ext>
where <ext> is chosen from the decompressed magic (psm2, psc3, psb4, bmpa,
bin otherwise).
"""
from __future__ import annotations

import argparse
import os
import struct
import sys
from collections import Counter
from typing import Iterator, Tuple

from ..baseline.lz_decoder import decode_bytes as lz_decode

CATEGORY_NAMES = {
    0x0000: "grp",
    0x0001: "scr",
    0x0002: "map",
    0x0003: "tex",
    0x0004: "itm",
    0x0006: "snd",
}


def iter_records(buf: bytes) -> Iterator[Tuple[int, int, int, int, bytes]]:
    """Yield (id, category, resource_id, offset, payload) per record."""
    p = 0
    n = len(buf)
    while p + 8 <= n:
        idv, size = struct.unpack_from("<II", buf, p)
        if idv == 0xFFFFFFFF:
            return
        if p + 8 + size > n:
            return
        cat = (idv >> 16) & 0xFFFF
        rid = idv & 0xFFFF
        yield idv, cat, rid, p, buf[p + 8 : p + 8 + size]
        p += 8 + size


def _classify(buf: bytes) -> str:
    if len(buf) < 4:
        return "bin"
    m = buf[:4]
    if m == b"PSM2":
        return "psm2"
    if m == b"PSC3":
        return "psc3"
    if m == b"PSB4":
        return "psb4"
    if m == b"BMPA":
        return "bmpa"
    return "bin"


def extract_bundle(bundle_path: str, dst_dir: str) -> dict:
    buf = open(bundle_path, "rb").read()
    os.makedirs(dst_dir, exist_ok=True)
    stats: Counter = Counter()
    manifest: list[str] = []
    for idv, cat, rid, off, payload in iter_records(buf):
        cat_name = CATEGORY_NAMES.get(cat, f"c{cat:04x}")
        try:
            decoded = lz_decode(payload)
        except Exception:
            decoded = payload
            stats["lz_fail"] += 1
        ext = _classify(decoded) if decoded else "bin"
        out_name = f"{cat_name}_{rid:04x}.{ext}"
        with open(os.path.join(dst_dir, out_name), "wb") as f:
            f.write(decoded)
        stats[f"{cat_name}_{ext}"] += 1
        stats["records"] += 1
        manifest.append(
            f"@0x{off:07x}  id=0x{idv:08x}  cat={cat_name}  "
            f"rid=0x{rid:04x}  packed={len(payload):>8}  "
            f"decoded={len(decoded):>8}  -> {out_name}"
        )
    with open(os.path.join(dst_dir, "_manifest.txt"), "w") as f:
        f.write("\n".join(manifest) + "\n")
    return dict(stats)


def run(src: str, dst: str, limit: int | None = None) -> None:
    os.makedirs(dst, exist_ok=True)
    total: Counter = Counter()
    bundles = sorted(fn for fn in os.listdir(src) if fn.endswith(".bin"))
    if limit is not None:
        bundles = bundles[:limit]
    for fn in bundles:
        stem = os.path.splitext(fn)[0]
        sub = os.path.join(dst, stem)
        stats = extract_bundle(os.path.join(src, fn), sub)
        for k, v in stats.items():
            total[k] += v
    print(f"Processed {len(bundles)} bundle(s).  Totals:")
    for k in sorted(total):
        print(f"  {k:<20} {total[k]}")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Unpack MCB1 scene bundles")
    ap.add_argument("--src", default="out/all/mcb",
                    help="Directory containing s{NN}_e{NNN}.bin bundle files")
    ap.add_argument("--dst", default="out/all/mcb_unpacked",
                    help="Output root (one subdir per bundle)")
    ap.add_argument("--limit", type=int, default=None,
                    help="Only process the first N bundles (debug)")
    args = ap.parse_args(argv)
    run(args.src, args.dst, limit=args.limit)
    return 0


if __name__ == "__main__":
    sys.exit(main())
