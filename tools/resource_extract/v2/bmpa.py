"""BMPA texture decoder (Orphen: Scion of Sorcery, PS2).

Format
------
All observed BMPA files are exactly 66,564 bytes and hold a single
256x256 8-bit indexed image with a 256-entry RGBA8888 palette.

    offset  size     field
    ------  ----     -----
      0      4       magic "BMPA"
      4      1024    palette: 256 entries * (R, G, B, A) bytes
                     (already in 0..255 range; no PS2 CLUT swizzle)
    1028      65536  image: 256*256 = 65536 bytes, one palette index per pixel,
                     row-major top-down

The 712 TEX.BIN entries extracted from the English disc are uniform on
this layout. MCB-embedded tex_* records from mcb_unpacked follow the
same layout.

Validated by writing PNGs for a sample set (0000/0001/0004/0100/0500):
no palette CLUT swizzle is required.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct
import zlib
from typing import Iterable


MAGIC = b"BMPA"
WIDTH = 256
HEIGHT = 256
PALETTE_BYTES = 256 * 4
IMAGE_BYTES = WIDTH * HEIGHT
TOTAL_BYTES = 4 + PALETTE_BYTES + IMAGE_BYTES  # 66564


@dataclass
class BmpaImage:
    palette: bytes  # 1024 bytes, RGBA8888 per entry
    indices: bytes  # WIDTH*HEIGHT bytes
    width: int = WIDTH
    height: int = HEIGHT


def parse(buf: bytes) -> BmpaImage:
    if len(buf) < TOTAL_BYTES:
        raise ValueError(f"BMPA too short: {len(buf)} < {TOTAL_BYTES}")
    if buf[:4] != MAGIC:
        raise ValueError(f"bad magic: {buf[:4]!r}")
    palette = bytes(buf[4 : 4 + PALETTE_BYTES])
    indices = bytes(buf[4 + PALETTE_BYTES : 4 + PALETTE_BYTES + IMAGE_BYTES])
    return BmpaImage(palette=palette, indices=indices)


def to_rgba(img: BmpaImage) -> bytes:
    """Expand the indexed image to raw RGBA8888 bytes, row-major top-down.

    Palette bytes are stored as B, G, R, A (PS2 GS-style), so we swap the
    first and third channels on the way out.
    """
    out = bytearray(img.width * img.height * 4)
    pal = img.palette
    idx = img.indices
    for i, v in enumerate(idx):
        o = i * 4
        p = v * 4
        out[o]     = pal[p + 2]  # R <- B
        out[o + 1] = pal[p + 1]  # G
        out[o + 2] = pal[p]      # B <- R
        out[o + 3] = pal[p + 3]  # A
    return bytes(out)


def write_png(img: BmpaImage, path: str | Path) -> None:
    """Write `img` as an RGBA PNG (uses only the stdlib).

    The pixel grid is flipped vertically on write: BMPA files store rows
    bottom-up (PS2 GS convention), so the raw index buffer renders upside
    down. Flipping here gives right-side-up PNGs.
    """
    rgba = to_rgba(img)

    raw = bytearray()
    stride = img.width * 4
    for y in range(img.height):
        raw.append(0)  # filter type None
        src_y = img.height - 1 - y
        raw.extend(rgba[src_y * stride : (src_y + 1) * stride])

    def _chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    ihdr = struct.pack(">IIBBBBB", img.width, img.height, 8, 6, 0, 0, 0)
    idat = zlib.compress(bytes(raw), 6)
    png = b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", ihdr) + _chunk(b"IDAT", idat) + _chunk(b"IEND", b"")
    Path(path).write_bytes(png)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _iter_inputs(src: Path) -> Iterable[Path]:
    if src.is_file():
        yield src
        return
    for p in sorted(src.rglob("*.bmpa")):
        yield p


def main(argv: list[str] | None = None) -> int:
    import argparse

    ap = argparse.ArgumentParser(description="Decode Orphen BMPA textures to PNG")
    ap.add_argument("--src", required=True, help="BMPA file, or directory to scan recursively for *.bmpa")
    ap.add_argument("--dst", required=True, help="Output directory for PNGs")
    ap.add_argument(
        "--flat",
        action="store_true",
        help="Write all PNGs flat into --dst (default preserves relative subdir structure)",
    )
    args = ap.parse_args(argv)

    src = Path(args.src)
    dst = Path(args.dst)
    dst.mkdir(parents=True, exist_ok=True)

    src_root = src if src.is_dir() else src.parent
    ok = 0
    bad = 0
    for p in _iter_inputs(src):
        try:
            img = parse(p.read_bytes())
        except Exception as exc:  # noqa: BLE001
            bad += 1
            print(f"  FAIL  {p}: {exc}")
            continue

        if args.flat or src.is_file():
            out = dst / (p.stem + ".png")
        else:
            rel = p.relative_to(src_root).with_suffix(".png")
            out = dst / rel
            out.parent.mkdir(parents=True, exist_ok=True)

        write_png(img, out)
        ok += 1

    print(f"Wrote {ok} PNG(s) to {dst}  ({bad} failed)")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
