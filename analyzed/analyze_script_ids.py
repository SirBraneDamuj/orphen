"""Extract specific 16-bit 'script ID' occurrences from a decompressed scr*.out file.

Outputs JSON listing each ID, file offset, and a hex context window to aid reverse engineering.

Usage (example):
  python analyze_script_ids.py scr2.out > scr2_script_ids.json

Design notes:
  - Header parsing (first 0x2C bytes) is included to compute pointer table end so we can classify
    each occurrence as pre/post pointer table.
  - Only scans the raw file; no relocation is applied (file offsets remain raw offsets).
  - Context window size kept modest (16 bytes before & after) to avoid overwhelming diff noise.
"""
from __future__ import annotations
import sys, json, pathlib

HEADER_SIZE = 0x2C

TARGET_IDS_DEC = [4927, 4947, 4519, 1194, 1204]

def read_header(fbytes: bytes):
    if len(fbytes) < HEADER_SIZE:
        raise ValueError("File too small for header")
    # 11 little-endian uint32
    words = [int.from_bytes(fbytes[i:i+4], 'little') for i in range(0, HEADER_SIZE, 4)]
    header = {
        'dialogue_start': words[0],
        'dialogue_end': words[1],
        'block2': words[2],
        'block3': words[3],
        'block4': words[4],
        'pointer_table_start': words[5],  # index 5 per prior analysis
        'pointer_table_end': words[6],    # index 6
        'footer_base': words[7],          # relocation chain base
        'triple_a': words[8],
        'triple_b': words[9],
        'triple_c': words[10],
        'raw_words': words,
    }
    return header

def scan_ids(fbytes: bytes, header: dict):
    ids = {}
    for dec in TARGET_IDS_DEC:
        le = dec.to_bytes(2, 'little')
        ids[dec] = []
        start = 0
        while True:
            idx = fbytes.find(le, start)
            if idx == -1:
                break
            context_before = fbytes[max(0, idx-16):idx]
            context_after = fbytes[idx+2: idx+2+16]
            ids[dec].append({
                'offset': idx,
                'offset_hex': f"0x{idx:05x}",
                'bytes_le': le.hex(),
                'pre_ptr_table': idx < header['pointer_table_end'],
                'context_before': context_before.hex(),
                'context_after': context_after.hex(),
            })
            start = idx + 2
    return ids

def main():
    if len(sys.argv) < 2:
        print("Usage: analyze_script_ids.py <scr*.out>", file=sys.stderr)
        sys.exit(1)
    path = pathlib.Path(sys.argv[1])
    data = path.read_bytes()
    header = read_header(data)
    result = {
        'file': str(path),
        'header': header,
        'script_ids': scan_ids(data, header),
    }
    json.dump(result, sys.stdout, indent=2)

if __name__ == '__main__':
    main()
