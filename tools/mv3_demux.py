#!/usr/bin/env python3
"""Demux Orphen MV3 files into code-derived MPEG and PCM payloads.

The layout is inferred from the game executable:
- src/FUN_002f1a70.c reads and validates the MV30 header.
- src/FUN_002f1c98.c copies fixed PCM and MPEG slices from each block.

This tool does not decode audio or video. It only reproduces the game's
container split so the MPEG payload can be tested with external decoders.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


HEADER = struct.Struct("<4sIIIII")
SECTOR_SIZE = 0x800


class Mv3Error(ValueError):
    pass


def parse_header(data: bytes) -> dict[str, int | str]:
    if len(data) < HEADER.size:
        raise Mv3Error(f"file is too small for an MV3 header ({len(data)} bytes)")

    magic, block_size, pcm_offset, pcm_size, mpeg_offset, mpeg_size = HEADER.unpack_from(data, 0)
    if magic != b"MV30":
        shown = magic.decode("ascii", errors="replace")
        raise Mv3Error(f"bad magic {shown!r}; expected 'MV30'")

    return {
        "magic": "MV30",
        "block_size": block_size,
        "pcm_offset": pcm_offset,
        "pcm_size": pcm_size,
        "mpeg_offset": mpeg_offset,
        "mpeg_size": mpeg_size,
    }


def require_valid_layout(header: dict[str, int | str], data_offset: int) -> None:
    block_size = int(header["block_size"])
    pcm_offset = int(header["pcm_offset"])
    pcm_size = int(header["pcm_size"])
    mpeg_offset = int(header["mpeg_offset"])
    mpeg_size = int(header["mpeg_size"])

    if data_offset < HEADER.size:
        raise Mv3Error(f"data offset 0x{data_offset:x} overlaps the header")
    if block_size <= 0:
        raise Mv3Error("block_size must be positive")
    if block_size % SECTOR_SIZE != 0:
        raise Mv3Error(f"block_size 0x{block_size:x} is not sector aligned")
    if pcm_offset + pcm_size > block_size:
        raise Mv3Error("PCM slice extends past the interleaved block")
    if mpeg_offset + mpeg_size > block_size:
        raise Mv3Error("MPEG slice extends past the interleaved block")

    if mpeg_size * 5 > 0x600000:
        raise Mv3Error("MPEG chunk size exceeds the game's 5-chunk buffer limit")
    if pcm_size * 8 > 0x200000:
        raise Mv3Error("PCM chunk size exceeds the game's 8-chunk buffer limit")


def demux(data: bytes, data_offset: int) -> tuple[dict[str, int | str], bytes, bytes, dict[str, int]]:
    header = parse_header(data)
    require_valid_layout(header, data_offset)

    block_size = int(header["block_size"])
    pcm_offset = int(header["pcm_offset"])
    pcm_size = int(header["pcm_size"])
    mpeg_offset = int(header["mpeg_offset"])
    mpeg_size = int(header["mpeg_size"])

    if len(data) < data_offset:
        raise Mv3Error(f"file is shorter than data offset 0x{data_offset:x}")

    payload_size = len(data) - data_offset
    block_count = payload_size // block_size
    trailing_bytes = payload_size % block_size

    pcm_chunks: list[bytes] = []
    mpeg_chunks: list[bytes] = []

    for block_index in range(block_count):
        start = data_offset + block_index * block_size
        block = data[start : start + block_size]
        pcm_chunks.append(block[pcm_offset : pcm_offset + pcm_size])
        mpeg_chunks.append(block[mpeg_offset : mpeg_offset + mpeg_size])

    stats = {
        "data_offset": data_offset,
        "block_count": block_count,
        "trailing_bytes": trailing_bytes,
        "pcm_payload_bytes": block_count * pcm_size,
        "mpeg_payload_bytes": block_count * mpeg_size,
    }
    return header, b"".join(pcm_chunks), b"".join(mpeg_chunks), stats


def default_prefix(path: Path) -> Path:
    return path.with_suffix("")


def write_outputs(prefix: Path, header: dict[str, int | str], pcm: bytes, mpeg: bytes, stats: dict[str, int]) -> None:
    prefix.parent.mkdir(parents=True, exist_ok=True)
    pcm_path = prefix.with_suffix(".pcm")
    mpeg_path = prefix.with_suffix(".mpg")
    info_path = prefix.with_suffix(".mv3.json")

    pcm_path.write_bytes(pcm)
    mpeg_path.write_bytes(mpeg)
    info_path.write_text(json.dumps({"header": header, "stats": stats}, indent=2) + "\n", encoding="utf-8")

    print(f"wrote {mpeg_path} ({len(mpeg)} bytes)")
    print(f"wrote {pcm_path} ({len(pcm)} bytes)")
    print(f"wrote {info_path}")
    if stats["trailing_bytes"]:
        print(f"warning: ignored {stats['trailing_bytes']} trailing byte(s) after complete blocks")


def main() -> int:
    parser = argparse.ArgumentParser(description="Demux an Orphen MV3 file into MPEG and PCM payloads.")
    parser.add_argument("mv3", type=Path, help="Path to an extracted .MV3 file")
    parser.add_argument("-o", "--out-prefix", type=Path, help="Output prefix; defaults to input without suffix")
    parser.add_argument(
        "--data-offset",
        type=lambda text: int(text, 0),
        default=SECTOR_SIZE,
        help="Offset where interleaved blocks begin; game default is 0x800",
    )
    args = parser.parse_args()

    data = args.mv3.read_bytes()
    header, pcm, mpeg, stats = demux(data, args.data_offset)
    write_outputs(args.out_prefix or default_prefix(args.mv3), header, pcm, mpeg, stats)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
