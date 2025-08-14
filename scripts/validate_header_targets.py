#!/usr/bin/env python3
"""Validate whether 4-byte block headers behave like relative length/skip fields under different interpretations.

For each block (first-variant JSON):
  header_value = little-endian uint32 of header bytes
  header_base  = start + 1 (byte immediately after 0x32)
  target_addr  = header_base + header_value
Interpretations supported:
    low8   : header_value = byte0
    low16  : header_value = little-endian uint16 (bytes 0-1)
    u32    : little-endian uint32 (bytes 0-3)

For each interpretation compute target_addr = header_base + header_value and classify:
    exact            : target_addr == end
    end_before_target: end < target_addr (header extends past actual block end)
    end_after_target : end > target_addr (block extends beyond header span)

Also compute ratios:
  payload_span = end - header_base
  coverage_ratio = payload_span / header_value (if header_value > 0)

This helps decide if header_value encodes total span length, a forward skip beyond end, or something else.

Usage:
    python analyzed/validate_header_targets.py blocks_scr2_first_v2.json --limit 20
    python analyzed/validate_header_targets.py blocks_scr2_first_v2.json --modes low8 low16 u32
"""
from __future__ import annotations
import argparse, json, os, statistics as st

def le_u32(hexstr: str) -> int:
    if len(hexstr) != 8:
        return -1
    b = bytes.fromhex(hexstr)
    return b[0] | (b[1]<<8) | (b[2]<<16) | (b[3]<<24)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('blocks_json')
    ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('--modes', nargs='*', default=['low8','low16','u32'], choices=['low8','low16','u32'])
    args = ap.parse_args()
    blocks = json.load(open(args.blocks_json,'r'))
    ladder_headers = {"5d000000","48000000","33000000","1e000000","09000000"}
    # Prepare structures per mode
    mode_stats = {}
    mode_rows = {}
    mode_ratios = {}
    ladder_rows = {m: [] for m in args.modes}
    for m in args.modes:
        mode_stats[m] = {'exact':0,'end_before_target':0,'end_after_target':0,'no_header':0}
        mode_rows[m] = []
        mode_ratios[m] = []
    for b in blocks:
        header = b.get('header','')
        if len(header) != 8:
            for m in args.modes:
                mode_stats[m]['no_header'] += 1
            continue
        raw = bytes.fromhex(header)
        start = b['start']; end = b['end']; header_base = start + 1
        payload_span = end - header_base
        interpretations = {}
        if 'low8' in args.modes:
            interpretations['low8'] = raw[0]
        if 'low16' in args.modes:
            interpretations['low16'] = raw[0] | (raw[1]<<8)
        if 'u32' in args.modes:
            interpretations['u32'] = le_u32(header)
        for m,hv in interpretations.items():
            target = header_base + hv
            classify = 'exact'
            if end < target:
                classify = 'end_before_target'
            elif end > target:
                classify = 'end_after_target'
            mode_stats[m][classify] += 1
            ratio = (payload_span / hv) if hv > 0 else 0
            mode_ratios[m].append(ratio)
            row = {'start':start,'end':end,'header':header,'hv':hv,'target':target,'class':classify,'ratio':ratio,'end_id16':b.get('end_id16')}
            mode_rows[m].append(row)
            if header in ladder_headers:
                ladder_rows[m].append(row)
    # Output summary per mode
    for m in args.modes:
        stats = mode_stats[m]
        total = stats['exact'] + stats['end_before_target'] + stats['end_after_target']
        print(f"== Mode {m} ==")
        print(f"Blocks with headers: {total}")
        for k,v in stats.items():
            if k != 'no_header':
                print(f"  {k}: {v}")
        ratios = mode_ratios[m]
        if ratios:
            print(f"Coverage ratio min/median/max: {min(ratios):.4f}/{st.median(ratios):.4f}/{max(ratios):.4f}")
        if args.limit:
            for r in mode_rows[m][:args.limit]:
                print(f"{r['start']:06x}-{r['end']:06x} header={r['header']} hv={r['hv']} target={r['target']:06x} class={r['class']} ratio={r['ratio']:.3f}")
        if ladder_rows[m]:
            print("  Ladder subset:")
            for r in sorted(ladder_rows[m], key=lambda x:x['start']):
                print(f"    {r['header']} hv={r['hv']:>5} start={r['start']:06x} target={r['target']:06x} end={r['end']:06x} class={r['class']} ratio={r['ratio']:.3f}")
        # Interpretive message
        if stats['exact'] and not stats['end_before_target'] and not stats['end_after_target']:
            print("  All exact -> interpretation matches block length exactly")
        elif stats['end_before_target'] and not stats['end_after_target'] and not stats['exact']:
            print("  All end_before_target -> header acts as upper bound / future convergence")
        elif stats['end_after_target'] and not stats['end_before_target'] and not stats['exact']:
            print("  All end_after_target -> header is too small (min/offset semantics)")
        else:
            print("  Mixed classifications -> header multipurpose / composite")
        print()

if __name__ == '__main__':
    main()
