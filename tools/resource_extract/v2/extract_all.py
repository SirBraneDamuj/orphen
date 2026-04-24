"""Top-level resource extractor.

Usage:
    python -m tools.resource_extract.v2.extract_all <SRC_DIR_OR_BIN> <DST>

What it does:
  * Detects file kind from filename (MAP.BIN, SCR.BIN, TEX.BIN, ITM.BIN, SND.BIN,
    GRP.BIN, VOICE.BIN → flat TOC; MCB0.BIN + MCB1.BIN → MCB bundle format).
  * For flat-TOC BINs: parses sector-0 TOC (see bin_toc.py), decompresses each
    entry with the headerless LZ decoder, and dumps it to DST/<bin>/<index>.bin.
    It also writes a sibling .ext hint when the magic is recognised (psm2/psc3/
    psb4/bmpa).
  * For MCB: delegates to `mcb.py` which already handles the pair.

No mesh parsing is invoked from here — downstream tools (psm2.py, psc3.py,
psb4.py, mcb_scan_meshes.py) operate on the dumped chunks.

Cross-ref: FUN_00223268 dispatches to FUN_00221b18..c90 for TOC lookup; those
confirm the sector-size packing (u32: low 17 bits = words, top 15 bits = sector).
MCB1 is loaded verbatim (no LZ) by FUN_00222898 into DAT_01949a00.
"""
from __future__ import annotations

import argparse
import os
import struct
import sys
from typing import Optional

from .bin_toc import iter_entries, parse_toc

# Reuse the headerless LZ decoder (baseline/lz_decoder.py = port of FUN_002f3118).
from ..baseline.lz_decoder import decode_bytes as lz_decode


# Files with the flat TOC format.
FLAT_TOC_BINS = {
    "GRP.BIN", "SCR.BIN", "MAP.BIN", "TEX.BIN",
    "ITM.BIN", "SND.BIN", "VOICE.BIN",
}


def classify_magic(buf: bytes) -> Optional[str]:
    if len(buf) < 4:
        return None
    m = buf[:4]
    if m == b"PSM2": return "psm2"
    if m == b"PSC3": return "psc3"
    if m == b"PSB4": return "psb4"
    if m == b"BMPA": return "bmpa"
    if m[:2] == b"BM": return "bmp"
    return None


def extract_flat_bin(path: str, dst_dir: str, decompress: bool = True) -> dict:
    """Extract every entry of a flat-TOC BIN into `dst_dir`.

    Returns a small stats dict.
    """
    os.makedirs(dst_dir, exist_ok=True)
    stats = {"entries": 0, "empty": 0, "ok": 0, "lz_fail": 0}
    magics: dict[str, int] = {}
    for e, raw in iter_entries(path):
        stats["entries"] += 1
        payload = raw
        if decompress:
            try:
                payload = lz_decode(raw)
            except Exception:
                stats["lz_fail"] += 1
                # Fall back to raw bytes so we never silently drop data.
                payload = raw
        if not payload:
            stats["empty"] += 1
            continue
        kind = classify_magic(payload) or "bin"
        magics[kind] = magics.get(kind, 0) + 1
        name = f"{e.index:04d}.{kind}"
        with open(os.path.join(dst_dir, name), "wb") as f:
            f.write(payload)
        stats["ok"] += 1
    stats["by_magic"] = magics
    return stats


def extract_mcb(mcb0: str, mcb1: str, dst_dir: str) -> dict:
    from . import mcb as mcb_mod
    written, magics = mcb_mod.extract(mcb0_path=mcb0, mcb1_path=mcb1,
                                       dst_dir=dst_dir)
    return {"written": written, "top_magics": magics.most_common(10)}


def run(src: str, dst: str, no_decompress: bool = False) -> None:
    if os.path.isdir(src):
        bins_found = []
        for name in sorted(os.listdir(src)):
            up = name.upper()
            if up in FLAT_TOC_BINS:
                bins_found.append((up, os.path.join(src, name)))
            elif up == "MCB0.BIN":
                mcb0 = os.path.join(src, name)
                mcb1 = os.path.join(src, "MCB1.BIN")
                if not os.path.exists(mcb1):
                    # try lowercase
                    mcb1 = os.path.join(src, "mcb1.bin")
                if os.path.exists(mcb1):
                    sub = os.path.join(dst, "mcb")
                    s = extract_mcb(mcb0, mcb1, sub)
                    print(f"MCB: {s}")
        for up, p in bins_found:
            sub = os.path.join(dst, up.replace(".BIN", "").lower())
            s = extract_flat_bin(p, sub, decompress=not no_decompress)
            print(f"{up}: {s}")
    else:
        up = os.path.basename(src).upper()
        if up in FLAT_TOC_BINS:
            s = extract_flat_bin(src, dst, decompress=not no_decompress)
            print(f"{up}: {s}")
        else:
            raise SystemExit(f"unsupported file: {src}")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Extract all resource archives")
    ap.add_argument("src", help="Directory containing .BIN files, or a single flat .BIN")
    ap.add_argument("dst", help="Output directory root")
    ap.add_argument("--no-decompress", action="store_true",
                    help="Skip LZ decompression (write raw compressed blobs)")
    args = ap.parse_args(argv)
    run(args.src, args.dst, no_decompress=args.no_decompress)
    return 0


if __name__ == "__main__":
    sys.exit(main())
