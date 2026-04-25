"""Decode the in-PSC3 animation timeline format.

Discovered from save state at out/capture/s00_e000_logo/eeMemory.bin
(grp_0183 / ORPHEN battle logo).

PSC3 layout (relevant fields, all u32 from psc3 base):
  +0x08  offs_submeshes
  +0x0C  offs_anim_table          (NEW: anim table is INTERNAL to PSC3)
  +0x2C  offs_keyframe_pool       (a.k.a. section_b)

Anim table:
  N records of 8 bytes:
    u32 timeline_off   ; psc3-relative offset into the timeline blob
    u32 lod_or_param   ; usually 0; for the master record this is 0x28

  The active anim is selected by `entity+0xa0` (u16 anim_id). The
  byte at `(anim_id*8 + anim_table + 4)` is read by FUN_0020c810 as
  an LOD/distance parameter.

Timeline blob (records 0..N-2):
  Stream of 6-byte entries:
    u16 target         ; high bit (0x8000) = END marker on this entry
    u16 duration       ; in frames
    u16 zero_pad       ; always 0

Master/compound timeline (last record, lod=0x28 in our sample):
  Different format. Starts with `cmd=0x02 arg=0xa70f pad=0`, then a
  list of psc3-relative back-pointers to the sub-timeline records.
  Likely: "play these N timelines in parallel on the right submeshes."
  (Format not fully nailed down — we only have one sample.)

Usage:
  python -m tools.resource_extract.v2.psc3_anim_decode <psc3_file>
  python -m tools.resource_extract.v2.psc3_anim_decode --eemem out/capture/s00_e000_logo/eeMemory.bin --psc3-addr 0xDF8E40
"""
from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass


@dataclass
class AnimRecord:
    index: int
    timeline_off: int
    lod_param: int


@dataclass
class TimelineEntry:
    target: int
    duration: int
    end: bool


def parse_anim_table(buf: bytes, anim_table_off: int) -> list[AnimRecord]:
    """Read 8-byte records until we hit padding (0x02020202) or run off."""
    out: list[AnimRecord] = []
    i = 0
    while True:
        a, b = struct.unpack_from("<II", buf, anim_table_off + i * 8)
        # 0x02020202 is observed PSC3 inter-section padding
        if a == 0x02020202:
            break
        # Sanity: timeline_off should be smaller than anim_table_off
        if a >= anim_table_off and i > 0:
            break
        out.append(AnimRecord(i, a, b))
        i += 1
        if i > 64:
            break
    return out


def parse_timeline(buf: bytes, off: int, end_off: int) -> list[TimelineEntry]:
    out: list[TimelineEntry] = []
    p = off
    while p + 6 <= end_off:
        target, duration, pad = struct.unpack_from("<HHH", buf, p)
        end = bool(duration & 0x8000)
        out.append(TimelineEntry(target, duration & 0x7FFF, end))
        p += 6
        if end:
            break
    return out


def decode(buf: bytes, base: int = 0) -> None:
    """`base` is the offset within `buf` where PSC3 magic lives."""
    if buf[base : base + 4] != b"PSC3":
        raise ValueError(f"PSC3 magic missing at base 0x{base:x}")
    (anim_table_off,) = struct.unpack_from("<I", buf, base + 0x0C)
    (kf_pool_off,) = struct.unpack_from("<I", buf, base + 0x2C)
    print(f"PSC3 base=0x{base:x}")
    print(f"  anim_table @ psc3+0x{anim_table_off:x}")
    print(f"  keyframe_pool @ psc3+0x{kf_pool_off:x}")
    print()

    recs = parse_anim_table(buf, base + anim_table_off)
    # Compute end of each timeline = start of the next record (or estimate)
    for i, r in enumerate(recs):
        if i + 1 < len(recs):
            end_off = base + recs[i + 1].timeline_off
        else:
            # Last record: use lod_param if it looks like a length, else cap at anim_table
            if 0 < r.lod_param < 0x200:
                end_off = base + r.timeline_off + r.lod_param
            else:
                end_off = base + anim_table_off
        is_master = i == len(recs) - 1 and r.lod_param == 0x28
        print(
            f"rec[{i}] timeline_off=0x{r.timeline_off:04x} "
            f"lod=0x{r.lod_param:x} len={end_off - (base + r.timeline_off)}"
            f"{'  [MASTER]' if is_master else ''}"
        )
        if is_master:
            # Master format: header u16 cmd, u16 arg, u16 pad ; then list of psc3-rel ptrs (u32 each? u16?).
            # Empirically: the back-pointers are u32 stride 8 with extra padding zeros.
            header = struct.unpack_from("<HHH", buf, base + r.timeline_off)
            print(f"      master_header = (cmd=0x{header[0]:x}, arg=0x{header[1]:x}, pad=0x{header[2]:x})")
            # Scan for plausible back-pointers (u32 values matching another record's timeline_off)
            valid = {rec.timeline_off for rec in recs[:-1]}
            p = base + r.timeline_off + 6
            while p + 4 <= end_off:
                (v,) = struct.unpack_from("<I", buf, p)
                if v in valid:
                    print(f"      -> rec(off=0x{v:04x})")
                p += 4
            continue
        # Normal timeline: walk 6-byte entries
        entries = parse_timeline(buf, base + r.timeline_off, end_off)
        total = sum(e.duration for e in entries)
        print(f"      entries={len(entries)} total_frames={total}")
        for j, e in enumerate(entries):
            tag = " END" if e.end else ""
            print(f"        [{j:2d}] target={e.target:3d}  duration={e.duration:5d}{tag}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("psc3", nargs="?", help="Path to a .psc3 file")
    ap.add_argument("--eemem", help="Path to PCSX2 eeMemory.bin")
    ap.add_argument(
        "--psc3-addr",
        type=lambda s: int(s, 0),
        help="PSC3 base address inside eeMemory (e.g. 0xDF8E40)",
    )
    args = ap.parse_args()
    if args.eemem and args.psc3_addr is not None:
        buf = open(args.eemem, "rb").read()
        decode(buf, args.psc3_addr)
    elif args.psc3:
        buf = open(args.psc3, "rb").read()
        decode(buf, 0)
    else:
        ap.print_help()
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
