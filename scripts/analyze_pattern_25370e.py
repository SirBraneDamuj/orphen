#!/usr/bin/env python3
"""Parse structured 'Pattern A' records in scr2.out region (~0x1990) of form:

  25 37 0e <ID> <zero_or_reserved_bytes?> 0b <param_bytes> 0b

Where:
  - Marker prefix: 25 37 0e
  - ID: single byte (observed set: 0x0e, 0x16-0x1e, 0x10-0x15)
  - After ID: sequence of (often zero) bytes until first delimiter 0x0b (call it delim1)
  - After delim1: param_bytes until next delimiter 0x0b (delim2)
  - Record ends at delim2; next record immediately follows if starts with 25 37 0e

We stop when pattern breaks or when encountering a different 0x25-prefixed signature
(transition region that begins with 25 01 52 0e ... at ~0x1a70 is treated as termination).

Outputs JSON with an ordered list of records including raw hex for param section and
simple numeric interpretations for common lengths (1 or 2 bytes LE).
"""
import json

FILENAME='scr2.out'
START=0x1990
MAX_SCAN=0x1b80  # stop before complex Pattern B zone fully

with open(FILENAME,'rb') as f:
    f.seek(START)
    data=f.read(MAX_SCAN-START)

records=[]
pos=0
while pos < len(data):
    # Look for marker
    if data[pos:pos+3] != b'\x25\x37\x0e':
        # If we see alternative 25 01 52 0e, terminate Pattern A parsing
        if data[pos:pos+4] == b'\x25\x01\x52\x0e':
            break
        pos += 1
        continue
    rec_start = START + pos
    if pos+4 >= len(data):
        break
    rec_id = data[pos+3]
    cursor = pos + 4
    # Field 1: bytes up to first 0x0b
    while cursor < len(data) and data[cursor] != 0x0b:
        cursor += 1
    if cursor >= len(data):
        break
    field1 = data[pos+4:cursor]
    if data[cursor] != 0x0b:
        break
    cursor += 1
    # Field 2 (param bytes) up to next 0x0b
    param_start = cursor
    while cursor < len(data) and data[cursor] != 0x0b:
        cursor += 1
    if cursor >= len(data):
        break
    param_bytes = data[param_start:cursor]
    cursor += 1  # skip second delimiter
    records.append({
        'offset': rec_start,
        'id': rec_id,
        'field1_hex': field1.hex(),
        'param_hex': param_bytes.hex(),
        'param_len': len(param_bytes),
        'param_u8': param_bytes[0] if len(param_bytes)==1 else None,
        'param_u16_le': int.from_bytes(param_bytes,'little') if len(param_bytes)==2 else None
    })
    pos = cursor

print(json.dumps({
    'pattern_a_record_count': len(records),
    'records': records
}, indent=2))
