#!/usr/bin/env python3
"""
PSC3 offline parser (skeleton)

Reads a decoded PSC3 blob, interprets key header fields and section offsets based on
code-backed analysis, and prints a JSON summary suitable for validation and tooling.

Notes
- The game treats fields at +0x08, +0x1C, +0x24 as offsets relative to the base.
- Relocation observed in runtime adjusts several header fields by a constant delta.
  For offline parsing of a decoded file, treat these section fields as base-relative offsets.
- This is a minimal, safe reader: it bounds-checks offsets/sizes and tolerates partial data.

Usage:
  python scripts/psc3_parser.py path/to/file.psc3.dec
"""
from __future__ import annotations
import argparse
import json
import struct
from dataclasses import dataclass, asdict
from typing import List, Optional, Tuple, Dict, Any

LE32 = "<I"
LE16 = "<H"
LE16S = "<h"

@dataclass
class PSC3Header:
    magic: int
    submesh_count: int
    offs_submesh_list: int
    offs_draw_desc: int
    offs_section_B: int
    offs_resource_table: int
    offs_section_D: int
    offs_subheader_40: int

    @staticmethod
    def parse(buf: bytes) -> "PSC3Header":
        if len(buf) < 0x44:
            raise ValueError("buffer too small for PSC3 header")
        magic, = struct.unpack_from(LE32, buf, 0x00)
        submesh_count, = struct.unpack_from(LE16, buf, 0x04)
        offs_submesh_list, = struct.unpack_from(LE32, buf, 0x08)
        # 0x0C,0x10,0x14,0x18 exist but are not currently needed here
        offs_draw_desc, = struct.unpack_from(LE32, buf, 0x1C)
        offs_section_B, = struct.unpack_from(LE32, buf, 0x20)
        offs_resource_table, = struct.unpack_from(LE32, buf, 0x24)
        offs_section_D, = struct.unpack_from(LE32, buf, 0x28)
        offs_subheader_40, = struct.unpack_from(LE32, buf, 0x40)
        return PSC3Header(
            magic=magic,
            submesh_count=submesh_count,
            offs_submesh_list=offs_submesh_list,
            offs_draw_desc=offs_draw_desc,
            offs_section_B=offs_section_B,
            offs_resource_table=offs_resource_table,
            offs_section_D=offs_section_D,
            offs_subheader_40=offs_subheader_40,
        )

@dataclass
class SubmeshEntry:
    raw: bytes  # 0x14 bytes
    stream_count_hint: int  # short at +6

    @staticmethod
    def parse(buf: bytes, base: int) -> "SubmeshEntry":
        if len(buf) < 0x14:
            raise ValueError("submesh entry too small")
        stream_hint, = struct.unpack_from(LE16, buf, 0x06)
        return SubmeshEntry(raw=buf[:0x14], stream_count_hint=stream_hint)

@dataclass
class DrawDesc:
    flags_u16: int         # +0x08
    a_u16: int             # +0x04
    b_u16: int             # +0x06
    stream_indices: Tuple[int, int, int, int]  # +0x0E,+0x10,+0x12,+0x14 (signed)

    @staticmethod
    def parse(buf: bytes) -> "DrawDesc":
        if len(buf) < 0x18:
            raise ValueError("draw desc too small")
        a_u16, = struct.unpack_from(LE16, buf, 0x04)
        b_u16, = struct.unpack_from(LE16, buf, 0x06)
        flags_u16, = struct.unpack_from(LE16, buf, 0x08)
        s0, = struct.unpack_from(LE16S, buf, 0x0E)
        s1, = struct.unpack_from(LE16S, buf, 0x10)
        s2, = struct.unpack_from(LE16S, buf, 0x12)
        s3, = struct.unpack_from(LE16S, buf, 0x14)
        return DrawDesc(
            flags_u16=flags_u16,
            a_u16=a_u16,
            b_u16=b_u16,
            stream_indices=(s0, s1, s2, s3),
        )

@dataclass
class ResourceRecord:
    raw10: bytes  # 10 bytes
    packed_u16: int  # +0x08

    @staticmethod
    def parse(buf: bytes) -> "ResourceRecord":
        if len(buf) < 10:
            raise ValueError("resource record too small")
        packed_u16, = struct.unpack_from(LE16, buf, 0x08)
        return ResourceRecord(raw10=buf[:10], packed_u16=packed_u16)

@dataclass
class PSC3Summary:
    header: PSC3Header
    submeshes: List[SubmeshEntry]
    draw_descs: List[DrawDesc]
    resources: List[ResourceRecord]

    def to_json(self) -> str:
        def enc(o: Any) -> Any:
            if hasattr(o, "__dict__"):
                return asdict(o)
            if isinstance(o, bytes):
                return o.hex()
            return o
        return json.dumps(asdict(self), default=enc, indent=2)


def _safe_slice(buf: bytes, start: int, size: int) -> bytes:
    end = start + size
    if start < 0 or size < 0 or end > len(buf):
        raise ValueError(f"slice out of bounds: start={start} size={size} len={len(buf)}")
    return buf[start:end]


def _compute_section_count(buf_len: int, section_off: int, stride: int, *stop_offs: int) -> int:
    """Conservative count: stop at the nearest non-zero offset or EOF.
    """
    candidates = [o for o in stop_offs if o]
    next_off = min([buf_len] + candidates)
    if section_off >= next_off:
        return 0
    size = next_off - section_off
    return max(0, size // stride)


def parse_psc3(buf: bytes) -> PSC3Summary:
    hdr = PSC3Header.parse(buf)
    if hdr.magic != 0x33435350:
        raise ValueError(f"bad magic: 0x{hdr.magic:08X} (expected 0x33435350)")

    base = 0  # offsets are relative to the buffer start

    # Submeshes
    submeshes: List[SubmeshEntry] = []
    if hdr.offs_submesh_list:
        subs_off = base + hdr.offs_submesh_list
        subs_cnt = min(
            hdr.submesh_count,
            _compute_section_count(len(buf), subs_off, 0x14, hdr.offs_draw_desc, hdr.offs_section_B,
                                    hdr.offs_resource_table, hdr.offs_section_D, hdr.offs_subheader_40),
        )
        for i in range(subs_cnt):
            entry = _safe_slice(buf, subs_off + i * 0x14, 0x14)
            submeshes.append(SubmeshEntry.parse(entry, base))

    # Draw descriptors (stride 0x18); count conservatively from following section or EOF
    draw_descs: List[DrawDesc] = []
    if hdr.offs_draw_desc:
        dd_off = base + hdr.offs_draw_desc
        dd_cnt = _compute_section_count(len(buf), dd_off, 0x18, hdr.offs_section_B,
                                        hdr.offs_resource_table, hdr.offs_section_D, hdr.offs_subheader_40)
        # Heuristic floor: at least submesh_count if available
        if hdr.submesh_count:
            dd_cnt = max(dd_cnt, hdr.submesh_count)
        for i in range(dd_cnt):
            try:
                entry = _safe_slice(buf, dd_off + i * 0x18, 0x18)
            except ValueError:
                break
            draw_descs.append(DrawDesc.parse(entry))

    # Resource records: 10 bytes each; derive count either by next section or by max stream index + 1
    resources: List[ResourceRecord] = []
    if hdr.offs_resource_table:
        res_off = base + hdr.offs_resource_table
        res_cnt = _compute_section_count(len(buf), res_off, 10, hdr.offs_section_D, hdr.offs_subheader_40)
        # Use stream indices to adjust expected resource entries
        max_idx = -1
        for dd in draw_descs:
            # last non -1 per FUN_00212058 behavior
            s = [x for x in dd.stream_indices if x >= 0]
            if s:
                max_idx = max(max_idx, s[-1])
        if max_idx >= 0:
            res_cnt = max(res_cnt, max_idx + 1)
        for i in range(res_cnt):
            try:
                entry = _safe_slice(buf, res_off + i * 10, 10)
            except ValueError:
                break
            resources.append(ResourceRecord.parse(entry))

    return PSC3Summary(
        header=hdr,
        submeshes=submeshes,
        draw_descs=draw_descs,
        resources=resources,
    )


def main() -> None:
    ap = argparse.ArgumentParser(description="PSC3 parser (skeleton)")
    ap.add_argument("input", help="path to decoded PSC3 file")
    ap.add_argument("--pretty", action="store_true", help="pretty-print summary JSON")
    ap.add_argument("--offset", type=lambda s: int(s, 0), default=0, help="byte offset where PSC3 starts (decimal or 0x...)")
    args = ap.parse_args()

    with open(args.input, "rb") as f:
        buf = f.read()

    if args.offset:
        if args.offset < 0 or args.offset >= len(buf):
            raise SystemExit(f"offset {args.offset} out of range for file length {len(buf)}")
        buf = buf[args.offset:]

    summary = parse_psc3(buf)
    if args.pretty:
        print(summary.to_json())
    else:
        # Compact JSON
        print(json.dumps(json.loads(summary.to_json()), separators=(",", ":")))


if __name__ == "__main__":
    main()
