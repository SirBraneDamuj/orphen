#!/usr/bin/env python3
"""Derive a provisional opcode->character mapping for the script VM.

Empirical observation from disassembly of scr2.out:
  Human-readable dialogue appears directly when decoding bytes as ASCII for ranges:
    - Letters, spaces, punctuation already in file, implying that many opcodes >=0x20 simply map 1:1 to displayed characters.
  The opcode disassembler shows these bytes currently classified as standard jump table handlers (FUN_....) rather than raw literals, meaning each printable character has a tiny handler that ultimately emits that glyph.

This tool:
  * Scans a decompressed sector file.
  * Identifies runs of ASCII-printable text (basic heuristic) that contain only bytes in an allowed set.
  * Counts per-byte frequency inside those runs.
  * Outputs a JSON mapping (byte value -> char) where the ASCII value equals the char and appears at least min_freq times.
  * Emits a Python module fragment for future hard-coding into the disassembler to directly render text sequences.

Limitations:
  * Control codes embedded between characters will split runs; mapping still valid for characters seen.
  * Some punctuation or special glyphs may use opcodes outside ASCII range; they will not be inferred here.
"""
import argparse, json, pathlib, string

PRINTABLE_BASE = set(ord(c) for c in string.ascii_letters + string.digits + string.punctuation + ' ')

# Accept standard printable ASCII minus control
ALLOWED = {b for b in PRINTABLE_BASE if 0x20 <= b <= 0x7e}

MIN_RUN = 4

def find_runs(data: bytes):
    runs=[]
    i=0
    n=len(data)
    while i<n:
        if data[i] in ALLOWED:
            j=i
            while j<n and data[j] in ALLOWED:
                j+=1
            if j-i >= MIN_RUN:
                runs.append((i,j))
            i=j
        else:
            i+=1
    return runs

def build_mapping(data: bytes, runs):
    freq={}
    for a,b in runs:
        for byte in data[a:b]:
            freq[byte]=freq.get(byte,0)+1
    mapping={f"0x{k:02X}": chr(k) for k,v in freq.items() if k in ALLOWED and v>=2}
    return mapping, freq


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--file', required=True)
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--min-freq', type=int, default=2)
    args=ap.parse_args()
    data=pathlib.Path(args.file).read_bytes()
    runs=find_runs(data)
    mapping, freq = build_mapping(data, runs)
    if args.json:
        json.dump({'mapping':mapping,'total_chars':len(mapping),'runs':len(runs)}, open(1,'w'), indent=2)
    else:
        print(f"Derived {len(mapping)} opcode->char entries from {len(runs)} text runs.")
        sample=list(mapping.items())[:20]
        print("Sample:")
        for k,v in sample:
            print(f"  {k} -> {v}")
        print("Python dict literal snippet (partial):")
        print("CHAR_OPCODE_MAP = {")
        for k,v in sorted(mapping.items()):
            print(f"    {k}: '{v.replace('\\','\\\\').replace('\'','\\\'')}',")
        print("}")

if __name__=='__main__':
    main()
