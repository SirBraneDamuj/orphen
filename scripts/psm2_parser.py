#!/usr/bin/env python3
"""
PSM2 offline parser (code-derived from src/FUN_0022b5a8.c)

What we extract (keeping to what code actually reads/writes):
- Header offsets at +0x04,+0x08,+0x0C,+0x10,+0x14,+0x18,+0x1C,+0x2C,+0x30,+0x34,+0x38
- Section A: s16 count, then 6 dwords per entry (we retain all 6 dwords)
- Section C: s16 count, then per entry reads 3 dwords via the generic dword reader; we expose them as floats
    plus a couple of convenience fields (u16_low/u8_hi_low) derived from the third dword as in the loader
- Section D: s16 count, then a packed stream of u16s per record. From code, each record consumes 32 bytes (16 u16).
    We extract just what we need for face assembly: four C-indices (first 4 u16) and the owning A-index found at
    byte offset +26 within the 32-byte record (13th u16). This matches code usage where that short indexes a 0x10
    stride table derived from Section A during load.
- Section J: s16 count, then 13 shorts per record copied into a 0x74-byte struct, followed by construction of a
    per-record float buffer. At runtime the buffer is populated with triplets built from a contiguous slice of Section C
    and an appended triplet from the owning A-record, with per-record base values subtracted. For exporting, we expose
    a convenience absolute vertex list per J-record built from the C-slice plus the appended A triplet (no subtraction),
    since faces and bounds elsewhere operate over the absolute C domain.

Outputs a JSON summary with per-J-record convenience vertices (absolute C-domain), the raw A/C/D values needed to
reconstruct faces, and minimal J metadata for cross-referencing.
"""
from __future__ import annotations
import argparse
import json
import struct
from dataclasses import dataclass, asdict
from typing import List, Tuple, Any

MAGIC_PSM2 = 0x324D5350
LE16 = '<h'
LE16U = '<H'
LE32U = '<I'
LE32F = '<f'

@dataclass
class ARecord:
    dwords: Tuple[int,int,int,int,int,int]
    # convenience views
    short0: int
    short1: int
    extra_xyz: Tuple[float,float,float]

@dataclass
class CRecord:
    f0: float
    f1: float
    f2: float
    # auxiliary fields decoded adjacent to the third dword in the loader
    b_index: int  # maps this C record to a Section B row (DAT_00355698)
    style_byte: int  # compact per-vertex flag byte (DAT_00355694)

@dataclass
class DRecord:
    # Minimal fields needed for face assembly
    a_index: int                 # from 0x78-record +0x10 (computed in code as u16; equals A index)
    indices: Tuple[int,int,int,int]  # four C-indices (from 0x80-record +0x24..+0x2A)

@dataclass
class JRecord:
    header_shorts: List[int]
    a_index: int
    c_index: int
    count: int
    # Absolute vertices in the Section C domain (plus the appended A triplet) for convenience
    vertices: List[Tuple[float,float,float]]

@dataclass
class PSM2Summary:
    offs: dict
    a_records: List[ARecord]
    c_records: List[CRecord]
    d_records: List[DRecord]
    j_records: List[JRecord]
    # Optional sections used by runtime vertex-source switching
    b_records: List[Tuple[float,float,float]]  # Section B rows (3 floats per row)

    def to_json(self) -> str:
        return json.dumps(asdict(self), indent=2)


def _u32(buf: bytes, off: int) -> int:
    return struct.unpack_from(LE32U, buf, off)[0]


def _s16(buf: bytes, off: int) -> int:
    return struct.unpack_from(LE16, buf, off)[0]


def _u16(buf: bytes, off: int) -> int:
    return struct.unpack_from(LE16U, buf, off)[0]


def _f32_from_u32(u: int) -> float:
    return struct.unpack(LE32F, struct.pack(LE32U, u))[0]


def parse_psm2(buf: bytes) -> PSM2Summary:
    if len(buf) < 0x3C:
        raise ValueError('buffer too small for PSM2 header')
    if _u32(buf, 0x00) != MAGIC_PSM2:
        raise ValueError(f'bad magic 0x{_u32(buf,0):08X}')
    offs = {
        'A': _u32(buf, 0x04),
        'C': _u32(buf, 0x08),
        'D': _u32(buf, 0x0C),
        'K': _u32(buf, 0x10),
        'E': _u32(buf, 0x14),
        'F': _u32(buf, 0x18),
        'J': _u32(buf, 0x1C),
        'G': _u32(buf, 0x2C),
        'B': _u32(buf, 0x30),
        'H': _u32(buf, 0x34),
        'I': _u32(buf, 0x38),
    }

    # Section A
    a_records: List[ARecord] = []
    if offs['A']:
        base = offs['A']
        cnt = _s16(buf, base)
        # next short at base+2 is used elsewhere; we skip here
        p = base + 2
        # The code copies 6 dwords into a 0x20 stride; read them directly
        p = base + 4  # after two shorts
        for i in range(max(0, cnt)):
            dwords = (
                _u32(buf, p + 0),
                _u32(buf, p + 4),
                _u32(buf, p + 8),
                _u32(buf, p + 12),
                _u32(buf, p + 16),
                _u32(buf, p + 20),
            )
            short0 = dwords[0] & 0xFFFF
            short1 = dwords[1] & 0xFFFF
            extra_xyz = (_f32_from_u32(dwords[2]), _f32_from_u32(dwords[3]), _f32_from_u32(dwords[4]))
            a_records.append(ARecord(dwords=dwords, short0=short0, short1=short1, extra_xyz=extra_xyz))
            p += 6*4

    # Section C
    c_records: List[CRecord] = []
    c_to_b: List[int] = []
    c_style: List[int] = []
    if offs['C']:
        base = offs['C']
        cnt = _s16(buf, base)
        p = base + 2
        for i in range(max(0, cnt)):
            # FUN_0022b4e0: read dword into aiStack_a0[0]
            d0 = _u32(buf, p); p += 4
            d1 = _u32(buf, p); p += 4
            d2 = _u32(buf, p); p += 4
            # After the third dword, the loader reads a u16 mapping into DAT_00355698,
            # and the following byte into DAT_00355694. Advance by a full dword.
            b_map = _u16(buf, p)
            style = buf[p+2] if (p+2) < len(buf) else 0
            p += 4
            c_to_b.append(b_map)
            c_style.append(style)
            c_records.append(CRecord(
                f0=_f32_from_u32(d0),
                f1=_f32_from_u32(d1),
                f2=_f32_from_u32(d2),
                b_index=b_map,
                style_byte=style,
            ))

    # Section D (minimal)
    d_records: List[DRecord] = []
    if offs['D']:
        base = offs['D']
        cnt = _s16(buf, base)
        p = base + 2
        for i in range(max(0, cnt)):
            if p + 32 > len(buf):
                break
            # First 4 u16 are the C-record indices
            s0 = _u16(buf, p + 0)
            s1 = _u16(buf, p + 2)
            s2 = _u16(buf, p + 4)
            s3 = _u16(buf, p + 6)
            # From analysis of FUN_0022b5a8, the u16 at byte offset +26 (13th short)
            # indexes a 0x10-stride table built from Section A during load; treat as A-index.
            a_index = _u16(buf, p + 26)
            d_records.append(DRecord(a_index=a_index, indices=(s0, s1, s2, s3)))
            p += 32  # each D-record consumes 32 bytes in the source stream

    # Section J
    j_records: List[JRecord] = []
    if offs['J']:
        base = offs['J']
        cnt = _s16(buf, base)
        p = base + 2
        for rec_i in range(max(0, cnt)):
            # 13 shorts
            if p + 26 > len(buf):
                break
            shorts = list(struct.unpack_from('<13h', buf, p))
            p += 26
            a_idx = shorts[0]
            # Resolve through Section A to get C index and count
            if 0 <= a_idx < len(a_records):
                arec = a_records[a_idx]
                c_index = arec.short0
                count = arec.short1
                # Build vertices from Section C float4 stream: xyz from records[c_index..c_index+count)
                verts: List[Tuple[float,float,float]] = []
                for k in range(max(0, count)):
                    idx = c_index + k
                    if 0 <= idx < len(c_records):
                        cr = c_records[idx]
                        verts.append((cr.f0, cr.f1, cr.f2))
                    else:
                        break
                # Append extra triplet from A-record dwords[2..4]
                verts.append(arec.extra_xyz)
            else:
                c_index = -1
                count = 0
                verts = []
            j_records.append(JRecord(
                header_shorts=shorts,
                a_index=a_idx,
                c_index=c_index,
                count=count,
                vertices=verts,
            ))

    # Section B (two identical tables in memory; we parse the first)
    b_records: List[Tuple[float,float,float]] = []
    if offs['B']:
        base = offs['B']
        # Count read via FUN_0022b4e0 (u32) then that many rows of 3 dwords
        cnt = _u32(buf, base)
        p = base + 4
        for i in range(cnt):
            if p + 12 > len(buf):
                break
            x = _f32_from_u32(_u32(buf, p)); p += 4
            y = _f32_from_u32(_u32(buf, p)); p += 4
            z = _f32_from_u32(_u32(buf, p)); p += 4
            # The fourth dword per row is cleared to zero in memory; ignore here
            b_records.append((x,y,z))

    return PSM2Summary(
        offs=offs,
        a_records=a_records,
        c_records=c_records,
        d_records=d_records,
        j_records=j_records,
        b_records=b_records,
    )


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description='PSM2 parser (code-derived)')
    ap.add_argument('input', help='path to decoded PSM2-containing file')
    ap.add_argument('--offset', type=lambda s: int(s, 0), default=0, help='offset of PSM2 (decimal or 0x..)')
    ap.add_argument('--pretty', action='store_true')
    args = ap.parse_args(argv)
    with open(args.input, 'rb') as f:
        data = f.read()
    if args.offset:
        if args.offset < 0 or args.offset >= len(data):
            raise SystemExit(f'offset {args.offset} out of range (len={len(data)})')
        data = data[args.offset:]
    try:
        summary = parse_psm2(data)
    except Exception as e:
        print(json.dumps({'error': str(e)}))
        return 1
    print(summary.to_json() if args.pretty else json.dumps(json.loads(summary.to_json()), separators=(',', ':')))
    return 0

if __name__ == '__main__':
    import sys
    raise SystemExit(main(sys.argv[1:]))
