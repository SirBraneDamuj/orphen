#!/usr/bin/env python3
"""
Scan scr2.out for putative subprocedure definition opcode patterns.
Hypothesis: 0x04 <u16 little-endian ID> marks a subproc definition.
User provided IDs were written big-endian in notes; we treat them as little-endian here.
Provide JSON output with context for each match and also enumerate all unique IDs encountered after 0x04.
"""
import json, sys, os
from collections import Counter

TARGET_IDS_BE = [0x4927,0x4953,0x47A1,0x04AA,0x04B4]  # as user wrote them (big-endian style notation per note)
# Convert to little-endian byte order values: e.g., 0x4927 -> bytes 27 49, interpreted as LE 0x2749
# But user's clarification: "little endian 4927 would be 0x3F 0x13" suggests original mapping different.
# Adjustment: They meant decimal? 0x133F (bytes 3F 13), 0x1353 (53 13), 0x11A7 (A7 11), 0x04AA (AA 04), 0x04B4 (B4 04).
# We'll use this corrected list of intended IDs.
INTENDED_IDS = [0x133F,0x1353,0x11A7,0x04AA,0x04B4]
INTENDED_ID_SET = set(INTENDED_IDS)

FILENAME = 'scr2.out'
# Number of bytes before and after match to capture
CONTEXT_BEFORE = 48
CONTEXT_AFTER = 64

with open(FILENAME,'rb') as f:
    data = f.read()

matches = []
all_after_04 = []

def parse_definition(buf, idx):
    """Heuristically parse a suspected subproc definition starting at opcode 0x04.
    Layout hypothesis (variant forms observed):
      04 <u16 id> 00 00 01 <u24 entry_offset> [0b 0b] <u32 length> ...
    Some records appear without the 00 00 pad or with sentinel 0b 0b inserted after a 3-byte offset.
    We'll attempt to detect these fields and return a dict.
    """
    out = {}
    # Ensure at least minimal bytes
    if idx+3 >= len(buf):
        return out
    out['id'] = buf[idx+1] | (buf[idx+2] << 8)
    cursor = idx + 3
    # Optional 00 00 padding
    if cursor+1 < len(buf) and buf[cursor] == 0x00 and buf[cursor+1] == 0x00:
        out['pad00'] = True
        cursor += 2
    # Expect 0x01 tag
    if cursor < len(buf) and buf[cursor] == 0x01:
        cursor += 1
        # Try 3-byte little endian entry offset
        if cursor+2 < len(buf):
            entry_off = buf[cursor] | (buf[cursor+1] << 8) | (buf[cursor+2] << 16)
            out['entry_offset'] = entry_off
            cursor += 3
    # Optional sentinel pair 0b 0b
    if cursor+1 < len(buf) and buf[cursor] == 0x0b and buf[cursor+1] == 0x0b:
        out['double_0b'] = True
        cursor += 2
    # Attempt a 32-bit little endian length if enough room and plausible (< 0x10000)
    if cursor+3 < len(buf):
        length = buf[cursor] | (buf[cursor+1] << 8) | (buf[cursor+2] << 16) | (buf[cursor+3] << 24)
        if 0 < length < 0x10000:
            out['length'] = length
            cursor += 4
    return out

for i in range(len(data)-8):  # need extra bytes for parsing
    if data[i] == 0x04:
        id_le = data[i+1] | (data[i+2] << 8)
        all_after_04.append(id_le)
        if id_le in INTENDED_ID_SET:
            start = max(0,i-CONTEXT_BEFORE)
            end = min(len(data), i+CONTEXT_AFTER)
            context_bytes = data[start:end]
            parsed = parse_definition(data, i)
            matches.append({
                'offset': i,
                'id': f"0x{id_le:04X}",
                'triple_hex': data[i:i+3].hex(),
                'parsed': parsed,
                'context_start': start,
                'context_hex': context_bytes.hex()
            })

id_counts = Counter(all_after_04)
summary = {
    'intended_ids': [f"0x{x:04X}" for x in INTENDED_IDS],
    'intended_matches': matches,
    'counts_top': id_counts.most_common(40),
    'unique_id_count': len(id_counts),
    'total_04_sequences': len(all_after_04)
}

json.dump(summary, sys.stdout, indent=2)
