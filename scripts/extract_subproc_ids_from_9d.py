#!/usr/bin/env python3
"""
Extract subproc IDs referenced by opcode 0x9D.

Heuristic based on analysis of opcode 0x9D (orig FUN_00261cb8):
- Layout: 0x9D <index-expr bytes ending with 0x0B> <offset u32 little-endian>
- Semantics: slot[index] = iGpffffb0e8 + offset
- Observation: The target at `offset` points to a subproc body; the subproc ID appears 4 bytes
  before that target location. Reading 2 bytes at (offset - 4), little-endian, yields the subproc ID.

This tool scans a script blob (default: scr2.out), finds all 0x9D opcodes,
parses up to the expression terminator (0x0B), reads the subsequent 4-byte offset, and then looks
back 4 bytes from that offset to extract a 16-bit ID. It reports all matches and flags anomalies.

Usage:
    python scripts/extract_subproc_ids_from_9d.py [-i INPUT] [-o OUTPUT_JSON] [--max-expr-len N]

Notes:
- We assume offsets are relative to the start of the provided file (script segment base).
- If an expression lacks a 0x0B within max-expr-len, the instance is reported as anomalous.
- If (offset-4) is out of bounds, the instance is reported as anomalous.

Outputs:
- Prints a summary table to stdout.
- Optionally writes a JSON array with detailed records if -o is provided.
"""
from __future__ import annotations
import argparse
import json
import os
from typing import List, Dict, Any

DEFAULT_CANDIDATES = ["scr2.out"]


def pick_default_input(cwd: str) -> str | None:
    for name in DEFAULT_CANDIDATES:
        p = os.path.join(cwd, name)
        if os.path.isfile(p):
            return p
    return None


def scan_9d(data: bytes, max_expr_len: int = 128) -> List[Dict[str, Any]]:
    results: List[Dict[str, Any]] = []
    i = 0
    n = len(data)
    while i < n:
        if data[i] != 0x9D:
            i += 1
            continue
        rec: Dict[str, Any] = {"pos_9d": i, "pos_9d_hex": f"0x{i:08x}"}
        j = i + 1
        # parse expression until 0x0B (return)
        k = j
        steps = 0
        term_pos = None
        while k < n and steps < max_expr_len:
            if data[k] == 0x0B:
                term_pos = k
                break
            k += 1
            steps += 1
        if term_pos is None:
            rec.update({
                "status": "anomaly:no_expr_terminator",
                "expr_preview": data[j:min(j+max_expr_len, n)].hex(),
            })
            results.append(rec)
            i += 1
            continue
        rec["expr_start"] = j
        rec["expr_end"] = term_pos
        # Expression bytes bookkeeping
        rec["index_expr_len_no_term"] = term_pos - j  # exclude terminator
        rec["index_expr_bytes"] = data[j:term_pos].hex()
        rec["index_expr_terminated_bytes"] = data[j:term_pos+1].hex()  # includes 0x0B
        # Provide legacy fields for compatibility
        rec["expr_len"] = (term_pos - j) + 1  # include terminator
        rec["expr_bytes"] = rec["index_expr_terminated_bytes"]
        # offset follows immediately after 0x0B
        off_pos = term_pos + 1
        if off_pos + 4 > n:
            rec.update({
                "status": "anomaly:truncated_offset",
            })
            results.append(rec)
            i = term_pos + 1
            continue
        offset_bytes = data[off_pos:off_pos+4]
        offset = int.from_bytes(offset_bytes, "little", signed=False)
        rec["offset_u32"] = offset
        rec["offset_bytes"] = offset_bytes.hex()
        target = offset
        rec["target_pos"] = target
        # subproc id: 2 bytes at (target - 4)
        id_pos = target - 4
        if id_pos < 0 or id_pos + 2 > n:
            rec.update({
                "status": "anomaly:id_oob",
                "context_before": data[max(0, target-16):min(n, target+8)].hex(),
            })
            results.append(rec)
        else:
            id_slice = data[id_pos:id_pos+2]
            subproc_id = int.from_bytes(id_slice, "little", signed=False)
            rec["subproc_id_le"] = subproc_id
            rec["subproc_id_hex"] = f"0x{subproc_id:04x}"
            rec["id_bytes"] = id_slice.hex()
            rec["id_pos"] = id_pos
            # provide 16-byte context ending at target
            ctx_lo = max(0, target - 16)
            rec["context_before"] = data[ctx_lo:target].hex()
            rec["status"] = "ok"
            results.append(rec)
        # advance scan: move past this 0x9D to avoid re-matching inside the same region
        i = off_pos + 4
    return results


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("-i", "--input", help="Input binary (default: scr2.out)")
    ap.add_argument("-o", "--output", help="Optional JSON output path")
    ap.add_argument("--max-expr-len", type=int, default=128, help="Max expression bytes to scan for 0x0B")
    args = ap.parse_args()

    inp = args.input
    if not inp:
        inp = pick_default_input(os.getcwd())
        if not inp:
            raise SystemExit("No input specified and default scr2.out not found. Provide -i <file>.")
    with open(inp, "rb") as f:
        data = f.read()

    results = scan_9d(data, max_expr_len=args.max_expr_len)

    # summary
    ok = [r for r in results if r.get("status") == "ok"]
    anomalies = [r for r in results if r.get("status", "").startswith("anomaly:")]

    print(f"Input: {inp}")
    print(f"Found 0x9D occurrences: {len(results)}")
    print(f"OK: {len(ok)}  Anomalies: {len(anomalies)}")
    print()
    print("Sample (first 20) OK entries:")
    for r in ok[:20]:
        print(
            f"@9D:{r['pos_9d']:08x} expr[{r['expr_len']}] -> offset {r['offset_u32']:08x} "
            f"target {r['target_pos']:08x} id {r['subproc_id_hex']} @ {r['id_pos']:08x}"
        )
    if anomalies:
        print()
        print("Anomalies:")
        for r in anomalies[:50]:
            print(f"@9D:{r['pos_9d']:08x} status={r['status']} offset={r.get('offset_u32')} target={r.get('target_pos')}")

    if args.output:
        out = {
            "input": inp,
            "count_total": len(results),
            "count_ok": len(ok),
            "count_anomalies": len(anomalies),
            "results": results,
        }
        with open(args.output, "w", encoding="utf-8") as f:
            json.dump(out, f, indent=2)
        print(f"\nWrote JSON: {args.output}")


if __name__ == "__main__":
    main()
