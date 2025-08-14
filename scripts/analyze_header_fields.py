#!/usr/bin/env python3
"""Header field decomposition and correlation analysis.

Fast pass to correlate each header byte with derived payload length metrics to identify any
"length-like" field across block families. Data-only; no modifications to source binaries.
"""
from __future__ import annotations
import argparse, json, statistics as st
from collections import Counter, defaultdict
from dataclasses import dataclass, field

@dataclass
class FamStat:
  count: int = 0
  b0_vals: Counter = field(default_factory=Counter)
  payloads: list[int] = field(default_factory=list)
  headers: Counter = field(default_factory=Counter)

LADDER = {"5d000000", "48000000", "33000000", "1e000000", "09000000"}

def main():
  ap = argparse.ArgumentParser()
  ap.add_argument('blocks_json')
  ap.add_argument('--limit', type=int, default=0, help='Print first N block decompositions')
  ap.add_argument('--top', type=int, default=8, help='Top N header families / byte values')
  args = ap.parse_args()
  blocks = json.load(open(args.blocks_json, 'r'))

  per_byte_values = {i: [] for i in range(4)}
  per_byte_ratios = {i: [] for i in range(4)}
  per_byte_matches = {i: 0 for i in range(4)}
  per_byte_counts = {i: Counter() for i in range(4)}
  ladder_rows = []
  fam_stats: dict[str, FamStat] = defaultdict(FamStat)

  rows = []
  for b in blocks:
    header = b.get('header', '')
    if len(header) != 8:
      continue
    hv = bytes.fromhex(header)
    b0, b1, b2, b3 = hv[0], hv[1], hv[2], hv[3]
    start = b['start']; end = b['end']
    header_base = start + 1  # first header byte position
    payload_len = end - header_base  # bytes between header start and terminator 0x04
    bytes_list = [b0, b1, b2, b3]
    for idx, val in enumerate(bytes_list):
      per_byte_values[idx].append(val)
      per_byte_counts[idx][val] += 1
      if val > 0:
        ratio = payload_len / val
        per_byte_ratios[idx].append(ratio)
        if val == payload_len:
          per_byte_matches[idx] += 1
    fam_key = header[2:]  # high 3 bytes as a family key
    fam = fam_stats[fam_key]
    fam.count += 1
    fam.b0_vals[b0] += 1
    fam.payloads.append(payload_len)
    fam.headers[header] += 1
    row = {
      'start': start, 'end': end, 'header': header,
      'b0': b0, 'b1': b1, 'b2': b2, 'b3': b3, 'payload_len': payload_len
    }
    rows.append(row)
    if header in LADDER:
      ladder_rows.append(row)

  print("Byte-level correlation:")
  for idx in range(4):
    values = per_byte_values[idx]
    ratios = [r for r in per_byte_ratios[idx] if r < 10000]
    nz = sum(1 for v in values if v > 0)
    print(f" b{idx}: nonzero={nz} exact_matches={per_byte_matches[idx]}")
    if ratios:
      print(f"   ratio min/med/max: {min(ratios):.3f}/{st.median(ratios):.3f}/{max(ratios):.3f}")
    print("   top vals:", per_byte_counts[idx].most_common(min(len(per_byte_counts[idx]), args.top)))

  print("\nHeader family (high 3 bytes) summary:")
  for fam_key, data in sorted(fam_stats.items(), key=lambda kv: kv[1].count, reverse=True)[:args.top]:
    payloads = data.payloads
    p_min = min(payloads); p_med = int(st.median(payloads)); p_max = max(payloads)
    b0_common = data.b0_vals.most_common(3)
    print(f" fam_high={fam_key} count={data.count} payload_len(min/med/max)={p_min}/{p_med}/{p_max} b0_top={b0_common}")

  if ladder_rows:
    print("\nLadder rows:")
    for r in sorted(ladder_rows, key=lambda x: x['start']):
      print(f"  {r['header']} b0={r['b0']:02x} payload={r['payload_len']} start={r['start']:06x}")

  if args.limit:
    print("\nSample decomposed rows:")
    for r in rows[:args.limit]:
      print(f" {r['start']:06x}-{r['end']:06x} {r['header']} b0={r['b0']:02x} b1={r['b1']:02x} b2={r['b2']:02x} b3={r['b3']:02x} payload={r['payload_len']}")

if __name__ == '__main__':
  main()
