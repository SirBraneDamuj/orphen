"""Scan SLUS_200.11 (and optionally an EE-RAM dump) for the static entity →
(mesh_id, tex_id) descriptor tables and emit a JSON sidecar.

Background (proven against an EE-RAM dump):
- FUN_00229980 routes entity-type lookups through several static tables. The
  pairing of grp_id (mesh) to tex_id is stored in stride-0x2C resource records,
  baked into .data. Only the +0x14 ptr field is mutated at runtime.
- Resource record layout:
    +0x00 u16 mesh_id (grp)
    +0x02 u16 tex_id
    +0x04 u32 sig/flags  (textured records have 'd' 'd' bytes at +5..+6)
    +0x14 u32 resource ptr (zero in ELF; non-zero when loaded into EE RAM)

Tables emitted (resource record bases):
  * DAT_0031ee48  — entity types 0x01..0x7B and 0x575+; in SLUS .data
  * DAT_003214f8  — entity types 0x7C..0xFA;            in SLUS .data
  * 0x0031a95c    — entity types 0x1F1..0x270 (direct); in SLUS .data
  * PTR_DAT_003228c0 → indirected table for types 0xFC..0x1EF. The pointer
                       points into BSS/heap (e.g. 0x01a2011c) which is NOT
                       in any PT_LOAD segment, so we can only read this
                       table from an EE-RAM dump.

Dynamic per-area tables routed through iGpffffb278[iVar5*8] (entity types
0x272..0x573) are also runtime-only; union them across EE dumps for full
coverage.

Usage:
    python tools/resource_extract/v2/scan_grp_tex_map.py \\
        --slus SLUS_200.11 \\
        --out  out/grp_tex_map.json \\
        [--eedump out/capture/<...>/eeMemory.bin]
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path


RESOURCE_STRIDE = 0x2C


# Tables backed directly in SLUS .data: (name, base_va, hard_max_slots)
_STATIC_TABLES: list[tuple[str, int, int]] = [
    ("DAT_0031ee48",   0x0031ee48, 0x200),
    ("DAT_003214f8",   0x003214f8, 0x200),
    ("addr_0031a95c",  0x0031a95c, 0x200),
]

# Indirected pointer table (BSS/heap target — needs an EE dump).
_INDIRECT_TABLE: tuple[str, int, int] = ("PTR_DAT_003228c0", 0x003228c0, 0x200)


# ---------------------------------------------------------------------------
# ELF helpers
# ---------------------------------------------------------------------------

def _parse_elf_loadable(elf: bytes) -> list[tuple[int, int, int]]:
    if elf[:4] != b"\x7fELF":
        raise ValueError("not an ELF file")
    e_phoff = struct.unpack("<I", elf[0x1C:0x20])[0]
    e_phentsize = struct.unpack("<H", elf[0x2A:0x2C])[0]
    e_phnum = struct.unpack("<H", elf[0x2C:0x2E])[0]
    out: list[tuple[int, int, int]] = []
    for i in range(e_phnum):
        o = e_phoff + i * e_phentsize
        p_type, p_off, p_va, _pa, p_fz, _mz, _f, _a = struct.unpack(
            "<IIIIIIII", elf[o : o + 32]
        )
        if p_type == 1:
            out.append((p_off, p_va, p_fz))
    return out


def _make_va2off(segs: list[tuple[int, int, int]]):
    def va2off(va: int) -> int | None:
        for off, v, fz in segs:
            if v <= va < v + fz:
                return off + (va - v)
        return None
    return va2off


# ---------------------------------------------------------------------------
# Table walk
# ---------------------------------------------------------------------------

def _walk_resource_table(buf: bytes, base_off: int, hard_max: int) -> list[dict]:
    """Walk stride-0x2C records, stopping at the first fully-zero record."""
    out: list[dict] = []
    for i in range(hard_max):
        o = base_off + i * RESOURCE_STRIDE
        rec = buf[o : o + RESOURCE_STRIDE]
        if len(rec) < RESOURCE_STRIDE:
            break
        if rec == b"\x00" * RESOURCE_STRIDE:
            break
        mesh_id, tex_id = struct.unpack("<HH", rec[:4])
        sig = struct.unpack("<I", rec[4:8])[0]
        ptr = struct.unpack("<I", rec[0x14:0x18])[0]
        out.append(
            {"slot": i, "mesh_id": mesh_id, "tex_id": tex_id, "sig": sig, "ptr": ptr}
        )
    return out


def _u32_le(buf: bytes, off: int) -> int:
    return struct.unpack("<I", buf[off : off + 4])[0]


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--slus", required=True, help="path to SLUS_200.11")
    ap.add_argument("--out",  required=True, help="output JSON path")
    ap.add_argument("--eedump", help="optional EE-RAM dump (32 MB, vaddr=offset)")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)

    elf = Path(args.slus).read_bytes()
    segs = _parse_elf_loadable(elf)
    va2off = _make_va2off(segs)

    ee_buf: bytes | None = None
    if args.eedump:
        ee_buf = Path(args.eedump).read_bytes()

    all_records: dict[str, list[dict]] = {}
    grp_to_tex: dict[int, list[int]] = {}  # mesh_id -> list of unique tex_ids in encounter order
    table_meta: list[dict] = []

    # 1) Static tables baked into ELF .data
    for tname, base_va, hard_max in _STATIC_TABLES:
        base_off = va2off(base_va)
        if base_off is None:
            print(f"  WARN: {tname} @ {base_va:#x} not in ELF; skipping",
                  file=sys.stderr)
            continue
        records = _walk_resource_table(elf, base_off, hard_max)
        all_records[tname] = records
        textured = sum(1 for r in records if r["mesh_id"] and r["tex_id"])
        print(f"  {tname} @ {base_va:#x}: {len(records)} records ({textured} textured)")
        table_meta.append({"name": tname, "base_va": base_va, "source": "elf",
                           "record_count": len(records)})

    # 2) Indirected table — requires EE dump (lives in BSS at runtime)
    iname, iva, ihard = _INDIRECT_TABLE
    iptr: int | None = None
    if ee_buf is not None and iva + 4 <= len(ee_buf):
        iptr_ee = _u32_le(ee_buf, iva)
        if 0x00100000 <= iptr_ee < 0x02000000 and iptr_ee + ihard * RESOURCE_STRIDE <= len(ee_buf):
            iptr = iptr_ee
            if args.verbose:
                print(f"  {iname}: EE pointer -> {iptr:#x}")
            records = _walk_resource_table(ee_buf, iptr, ihard)
            all_records[iname] = records
            textured = sum(1 for r in records if r["mesh_id"] and r["tex_id"])
            print(f"  {iname} -> {iptr:#x}: {len(records)} records "
                  f"({textured} textured) [from EE dump]")
            table_meta.append({"name": iname, "base_va": iptr, "source": "eedump",
                               "record_count": len(records)})
    if iptr is None:
        print(f"  {iname}: not available (BSS/heap; supply --eedump for this table)")

    # 3) Aggregate mapping
    for tname, records in all_records.items():
        for r in records:
            mid, tid = r["mesh_id"], r["tex_id"]
            if not mid or not tid:
                continue
            grp_to_tex.setdefault(mid, [])
            if tid not in grp_to_tex[mid]:
                grp_to_tex[mid].append(tid)

    primary_map: dict[str, str] = {}
    multi_tex: dict[str, list[str]] = {}
    for mid in sorted(grp_to_tex):
        tids = grp_to_tex[mid]
        primary_map[f"{mid:04x}"] = f"{tids[0]:04x}"
        if len(tids) > 1:
            multi_tex[f"{mid:04x}"] = [f"{t:04x}" for t in sorted(tids)]

    # 4) Optional cross-check of static tables against EE dump
    cross_check: dict | None = None
    if ee_buf is not None:
        compared = 0
        mismatches: list[dict] = []
        for tname, base_va, _ in _STATIC_TABLES:
            for r in all_records.get(tname, []):
                va = base_va + r["slot"] * RESOURCE_STRIDE
                if va + 4 > len(ee_buf):
                    continue
                ee_mid, ee_tid = struct.unpack("<HH", ee_buf[va : va + 4])
                compared += 1
                if (ee_mid, ee_tid) != (r["mesh_id"], r["tex_id"]):
                    mismatches.append(
                        {"table": tname, "slot": r["slot"], "va": va,
                         "elf": [r["mesh_id"], r["tex_id"]],
                         "ee":  [ee_mid, ee_tid]}
                    )
        cross_check = {"compared": compared, "mismatches": mismatches}
        print(f"  EE cross-check (static tables only): "
              f"{compared} records compared, {len(mismatches)} mismatches")

    out = {
        "source_slus": str(args.slus),
        "source_eedump": str(args.eedump) if args.eedump else None,
        "tables": table_meta,
        "grp_to_tex": primary_map,
        "grp_to_tex_alternates": multi_tex,
        "raw_records": all_records,
    }
    if cross_check is not None:
        out["ee_cross_check"] = cross_check

    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out).write_text(json.dumps(out, indent=2))
    print(f"  wrote {args.out}: {len(primary_map)} unique grp ids "
          f"({len(multi_tex)} with alternate textures)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
