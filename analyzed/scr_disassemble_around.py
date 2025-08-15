"""
Structural disassemble-around helper.

Given an offset (absolute into scr2.out) or a SUBPROC ID16, print a small window
of structural opcodes around the nearest 0x32 site and annotate the VM payload
just after it. Additionally:

- Detect and label the SUBPROC prologue motif (0B 04 <id16_le>) and the common
    variant (0B 0B 04 <id16_le>), derive the SUBPROC ID, and report the likely
    entry pointer (first non-zero byte following the prologue, typically 0x01).
- Confirm the runtime correlation by reading *(entry-4) as ID16 when possible.
- Heuristically hint at loops by scanning the immediate stream right after 0x32
    and flagging any negative immediate values (potential backward displacements).

Grounded on src/FUN_0025bc68 (structural) and FUN_0025bf70 (VM immediates).
"""
from __future__ import annotations

import argparse
import pathlib
from typing import Iterable, Tuple, List, Optional

from vm_immediate_walker import walk_vm_immediates


def scan_struct_32_sites(buf: bytes) -> List[int]:
    return [i for i, b in enumerate(buf) if b == 0x32]


def find_nearest(hay: List[int], off: int) -> int:
    import bisect
    i = bisect.bisect_left(hay, off)
    if i == 0:
        return hay[0]
    if i >= len(hay):
        return hay[-1]
    before = hay[i - 1]
    after = hay[i]
    return before if off - before <= after - off else after


def pretty_struct_window(buf: bytes, center: int, radius: int = 24) -> str:
    lo = max(0, center - radius)
    hi = min(len(buf), center + radius)
    seg = buf[lo:hi]
    out = []
    for i, b in enumerate(seg, start=lo):
        mark = "<" if i == center else " "
        out.append(f"{i:08x}:{mark} {b:02x}")
    return "\n".join(out)


def main(argv: List[str]) -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("file", nargs="?", default="scr2.out")
    ap.add_argument("--offset", type=lambda x: int(x, 0), help="Absolute offset of interest")
    ap.add_argument("--id16", type=lambda x: int(x, 0), help="SUBPROC ID16 to seek via raw motif 0B 04 <id16>")
    ap.add_argument("--vm-scan", type=int, default=64, help="Bytes to scan after payload for immediate-based loop hints")
    ap.add_argument("--int-scan", type=int, default=0, help="Bytes to scan near entry for negative 32-bit values (compiled stream loop hints). 0 disables.")
    ap.add_argument("--show-broad", action="store_true", help="Show broad negative int32 scan results when --int-scan > 0")
    # Removed VM re-entry (0x32) heuristic; flags deprecated
    args = ap.parse_args(argv)

    data = pathlib.Path(args.file).read_bytes()

    if args.offset is None and args.id16 is None:
        raise SystemExit("Provide --offset or --id16")

    target_off = None
    tag_hit_off: Optional[int] = None
    if args.offset is not None:
        target_off = args.offset
    else:
        # raw scan for 0B 04 <id16_le>
        tag = bytes([0x0B, 0x04, args.id16 & 0xFF, (args.id16 >> 8) & 0xFF])
        i = 0
        while True:
            j = data.find(tag, i)
            if j < 0:
                break
            # Heuristic: consider this payload sits right after a nearby 0x32
            # Find nearest 0x32 to j-?; we’ll just take nearest in file for now.
            target_off = j
            tag_hit_off = j
            break
        if target_off is None:
            raise SystemExit(f"ID16 {args.id16} tag not found")

    sites = scan_struct_32_sites(data)
    if not sites:
        raise SystemExit("No 0x32 sites found")

    near = find_nearest(sites, target_off)
    payload_off = near + 1  # structural 0x32 is followed by VM payload (at least 4 bytes)

    print("== structural window around nearest 0x32 ==")
    print(pretty_struct_window(data, near))

    print("\n== VM immediate decoding of payload ==")
    items = walk_vm_immediates(data, payload_off, max_items=8)
    for item in items:
        val = f" value={item.value}" if item.value is not None else ""
        print(f"{item.off:08x}: op={item.op:02x} {item.desc}{val}")

    # Prologue/ID derivation and entry pointer heuristic
    print("\n== SUBPROC prologue analysis ==")
    entry_ptr: Optional[int] = None
    id16_from_prologue: Optional[int] = None
    prologue_len = 0
    prologue_at = None
    search_start = tag_hit_off if tag_hit_off is not None else payload_off
    # Support two observed motifs: 0B 04 <id16> and 0B 0B 04 <id16>
    # Search a small window to accommodate nearby layout variations.
    for delta in range(0, 32):
        i = search_start + delta
        if i + 5 <= len(data) and data[i] == 0x0B and data[i + 1] == 0x0B and data[i + 2] == 0x04:
            id16_from_prologue = data[i + 3] | (data[i + 4] << 8)
            prologue_len = 5
            prologue_at = i
            break
        if i + 4 <= len(data) and data[i] == 0x0B and data[i + 1] == 0x04:
            id16_from_prologue = data[i + 2] | (data[i + 3] << 8)
            prologue_len = 4
            prologue_at = i
            break
    if prologue_at is not None:
        if prologue_len == 5:
            print(f"prologue: {prologue_at:08x}: 0B 0B 04 {data[prologue_at+3]:02x} {data[prologue_at+4]:02x}  (ID16={id16_from_prologue})")
        else:
            print(f"prologue: {prologue_at:08x}: 0B 04 {data[prologue_at+2]:02x} {data[prologue_at+3]:02x}  (ID16={id16_from_prologue})")
        # Heuristic: skip any zero padding after prologue to locate first opcode byte (often 0x01)
        p = prologue_at + prologue_len
        while p < len(data) and data[p] == 0x00 and (p - (payload_off + prologue_len)) < 8:
            p += 1
        entry_ptr = p if p < len(data) else None
        if entry_ptr is not None:
            # Confirm *(entry-4) if safely addressable
            if entry_ptr >= 4:
                id16_at_entry_minus4 = data[entry_ptr - 4] | (data[entry_ptr - 3] << 8)
                print(f"entry:    {entry_ptr:08x}: byte={data[entry_ptr]:02x}  (confirm *(entry-4)={id16_at_entry_minus4})")
            else:
                print(f"entry:    {entry_ptr:08x}: byte={data[entry_ptr]:02x}  (cannot read *(entry-4))")
        else:
            print("entry:    could not derive entry pointer (EOF)")
    else:
        where = "tag hit" if tag_hit_off is not None else "immediate payload"
        print(f"prologue: no recognizable 0B 04 / 0B 0B 04 motif within +0..+31 of {where}")

    # Loop hinting: scan a short range of immediates after payload and flag any negative values
    # Note: We don't decode VM opcodes here; this is a heuristic signal only.
    print("\n== loop hints (heuristic) ==")
    neg_hits: List[str] = []
    scan_limit = max(0, min(len(data) - payload_off, args.vm_scan))
    # Reuse the immediate walker but recompute signed interpretations for 16/32-bit immediates
    imms = walk_vm_immediates(data, payload_off, max_items=32)
    for it in imms:
        if it.op == 0x0D and it.size == 3:
            # Read raw u16 then sign-extend
            if it.off + 3 <= len(data):
                raw = data[it.off + 1 : it.off + 3]
                sval = int.from_bytes(raw, "little", signed=True)
                if sval < 0:
                    neg_hits.append(f"{it.off:08x}: IMM16 {sval}")
        elif it.op == 0x0E and it.size == 5:
            if it.off + 5 <= len(data):
                raw = data[it.off + 1 : it.off + 5]
                sval = int.from_bytes(raw, "little", signed=True)
                if sval < 0:
                    neg_hits.append(f"{it.off:08x}: IMM32 {sval}")
        # Stop scanning once beyond requested range
        if it.off >= payload_off + scan_limit:
            break
    if neg_hits:
        print("potential backward displacement immediates detected:")
        for s in neg_hits[:8]:
            print("  ", s)
    else:
        print("no negative immediates observed in scan window")

    # Termination heuristic: look for an early structural return boundary (0x04) after entry.
    if entry_ptr is not None:
        print("\n== termination hints (heuristic) ==")
        # Scan forward a small window for a 0x04 block-end which causes structural interpreter to pop/return.
        # This is a strong indicator the subproc self-terminates quickly.
        term_window = data[entry_ptr : min(len(data), entry_ptr + 64)]
        try:
            rel = term_window.index(0x04)
            print(f"early structural 0x04 found at +{rel} bytes from entry (likely short-lived)")
        except ValueError:
            print("no early 0x04 found near entry (could be persistent/looping)")

    # Optional: compiled int-stream scan for negative values (off by default to reduce noise)
    if entry_ptr is not None and args.int_scan > 0:
        print("\n== compiled-stream loop hints (heuristic) ==")
        window = data[entry_ptr : min(len(data), entry_ptr + args.int_scan)]
        hits: List[str] = []
        for align in range(4):
            for off in range(align, len(window) - 3, 4):
                sval = int.from_bytes(window[off : off + 4], "little", signed=True)
                if sval < 0:
                    abs_off = entry_ptr + off
                    if args.show_broad:
                        prevb = data[abs_off - 1] if abs_off - 1 >= 0 else None
                        prev = f" prev={prevb:02x}" if prevb is not None else ""
                        hits.append(f"{abs_off:08x}: int32 {sval}{prev}")
                    else:
                        hits.append(f"{abs_off:08x}")
        if hits:
            print("potential backward relative displacements in compiled stream (count=", len(hits), ")")
            if args.show_broad:
                for s in hits[:12]:
                    print("  ", s)
        else:
            print("no negative 32-bit values found near entry")

    # VM re-entry heuristic removed per latest analysis


if __name__ == "__main__":
    import sys
    main(sys.argv[1:])
