#!/usr/bin/env python3
"""Best-effort parser for post-Pattern-A structure beginning with bytes 25 01 52 0e.

Context:
  After the contiguous Pattern A (25 37 0e <id> ... 0b <params> 0b) records end
  near offset ~0x1A6C in scr2.out, a new repeating structure appears. It starts
  with the 4-byte signature 25 01 52 0e, followed by a 32-bit little-endian
  value, delimiter 0x0B, then a sub-header sequence beginning 0B 34 00 00 00
  37 0e <index32> 0B 59 0B ... and nested clusters of 79 0e / 0e <val> command
  pairs. Subsequent blocks use 01 52 0e (without leading 25) plus the value.

Observed (scr2.out excerpts):
  25 01 52 0e 03 00 00 00 0b 0b 34 00 00 00 37 0e 00 00 00 00 0b 59 0b
  25 79 0e 00 01 00 00 0b 0e 04 00 00 00 0b 0e 01 00 00 00 0b 79 0e ...
  01 52 0e 04 00 00 00 0b 0b 34 00 00 00 37 0e 01 00 00 00 0b 59 0b ...

Hypothesis:
  - 01 52 0e sequence denotes a table/array of blocks; initial block uses 0x25
    prefix as a start marker (matching Pattern A style of a 0x25 leading byte).
  - The 34 00 00 00 might be a constant size or type tag for the following
    structure; 37 0e <index> stores a block index; 59 may be a delimiter opcode.
  - 79 0e <value32> records each followed by parameter-setting opcodes 0e 04 ...
    and 0e 01 ... could define a pair of attributes for that value (e.g. timing
    & mode).

Goal of this script:
  Extract a machine-readable JSON summarizing:
    - Each top-level block (sequence number, raw value after 01 52 0e)
    - Optional subheader (presence of 0x34, index after 37 0e)
    - List of 79 0e subrecords with their value and trailing parameter sets.

Parsing Strategy (heuristic, tolerant):
  1. Locate initial b'\x25\x01\x52\x0e'. If absent, exit.
  2. For each block: accept either prefix 25 01 52 0e or 01 52 0e.
  3. Read <block_value> (u32 LE) then expect delimiter 0x0b (skip any extra 0x0b).
  4. Attempt to parse subheader: if next bytes match 0x34 00 00 00 after a 0x0b
     (or double 0x0b), then parse index sequence: expect 37 0e <u32> 0x0b 59 0x0b.
  5. Parse nested 79 0e clusters until next block signature or EOF.
     - A 79 cluster may have optional leading 0x25 for first occurrence.
     - Format: [optional 0x25] 79 0e <u32 value> 0x0b then zero or more parameter
       opcodes of form 0e <valByte> 00 00 00 0b. Collect valByte list.
  6. Detect next block by scanning ahead for 0x01 0x52 0x0e preceded by either
     0x25 (start) or 0x0b (separator). We stop when found; remainder is next block.

Output:
  JSON with keys: pattern_b_block_count, blocks (list). Each block dict contains:
    offset, has_start_marker, block_value, subheader_index (or null), subrecords.
  Each subrecord: offset, has_start_marker, value, params (list of ints).

Limitations:
  - Script does not assert strict structural validity; it aims to extract
    repeatable patterns to aid further reverse-engineering.
  - If unexpected bytes appear, the parser will bail out of current block and
    resume scanning for the next signature to minimize cascading errors.
"""
import json

FILENAME = 'scr2.out'
START_SEARCH = 0x1800  # search window starts before expected region for safety
END_LIMIT = 0x1d00      # cap to avoid running into later structures for now

with open(FILENAME, 'rb') as f:
    f.seek(START_SEARCH)
    data = f.read(END_LIMIT - START_SEARCH)

def find_initial(data: bytes):
    sig = b'\x25\x01\x52\x0e'
    idx = data.find(sig)
    return idx

def u32(b: bytes) -> int:
    return int.from_bytes(b, 'little')

blocks = []
pos0 = find_initial(data)
if pos0 < 0:
    print(json.dumps({'pattern_b_block_count': 0, 'blocks': []}, indent=2))
    raise SystemExit

pos = pos0
ABS = START_SEARCH

def peek(data, pos, length):
    return data[pos:pos+length]

def scan_next_block_start(data, start_pos):
    # Search for '\x01\x52\x0e' optionally with leading 0x25; ensure not at current position.
    i = start_pos
    while i < len(data)-3:
        if data[i] == 0x01 and data[i+1] == 0x52 and data[i+2] == 0x0e:
            return i, False
        if data[i] == 0x25 and data[i+1] == 0x01 and data[i+2] == 0x52 and data[i+3] == 0x0e:
            return i, True
        i += 1
    return -1, False

while pos < len(data):
    # Determine header form
    has_start_marker = False
    if peek(data,pos,4) == b'\x25\x01\x52\x0e':
        has_start_marker = True
        hdr_len = 4
    elif peek(data,pos,3) == b'\x01\x52\x0e':
        hdr_len = 3
    else:
        # No more recognizable blocks
        break
    block_start_abs = ABS + pos
    val_pos = pos + hdr_len
    if val_pos + 4 > len(data):
        break
    block_value = u32(peek(data,val_pos,4))
    cursor = val_pos + 4
    # Expect at least one 0x0b delimiter; skip any consecutive 0x0b
    if cursor < len(data) and data[cursor] == 0x0b:
        while cursor < len(data) and data[cursor] == 0x0b:
            cursor += 1
    else:
        # Malformed; move on by one byte
        pos += 1
        continue
    # Optional subheader: 34 00 00 00 37 0e <u32> 0b 59 0b (with possible preceding 0x0b already consumed)
    subheader_index = None
    save_cursor = cursor
    if peek(data,cursor,4) == b'\x34\x00\x00\x00' and peek(data,cursor+4,2) == b'\x37\x0e':
        cursor += 6
        if cursor+4 <= len(data):
            subheader_index = u32(peek(data,cursor,4))
            cursor += 4
            # Expect 0x0b 0x59 0x0b pattern
            if cursor+3 <= len(data) and data[cursor] == 0x0b and data[cursor+1] == 0x59 and data[cursor+2] == 0x0b:
                cursor += 3
            else:
                # Revert if pattern not satisfied
                subheader_index = None
                cursor = save_cursor
        else:
            cursor = save_cursor
    # Parse 79 0e clusters
    subrecords = []
    while cursor < len(data):
        # Look ahead for next block start to bound current parsing
        next_blk_rel, next_has_marker = scan_next_block_start(data, cursor)
        next_block_abs_pos = ABS + next_blk_rel if next_blk_rel >=0 else None
        # Try parse a 79 cluster only if it appears before the next block start (or no next block)
        # Accept optional 0x25 prefix once at cluster beginning
        local_cursor = cursor
        cluster_has_marker = False
        if local_cursor < len(data) and data[local_cursor] == 0x25 and peek(data, local_cursor+1,2) == b'\x79\x0e':
            cluster_has_marker = True
            local_cursor += 1
        if peek(data, local_cursor,2) != b'\x79\x0e':
            # If next block start is here, break outer subrecord loop; else advance one byte
            if next_blk_rel >=0 and next_block_abs_pos is not None and (ABS + cursor) >= next_block_abs_pos:
                break
            # Non-matching noise; advance cursor carefully until potential block start or end
            if next_blk_rel >=0 and cursor >= next_blk_rel:
                break
            cursor += 1
            continue
        # At this point we have 79 0e
        local_cursor += 2
        if local_cursor + 4 > len(data):
            break
        value = u32(peek(data, local_cursor,4))
        local_cursor += 4
        # Expect delimiter 0x0b
        if local_cursor >= len(data) or data[local_cursor] != 0x0b:
            cursor += 1
            continue
        local_cursor += 1
        params = []
        # Parse trailing parameter opcodes: pattern 0e <valByte> 00 00 00 0b
        while local_cursor + 6 <= len(data) and data[local_cursor] == 0x0e:
            if local_cursor + 6 > len(data):
                break
            val_byte = data[local_cursor+1]
            if peek(data, local_cursor+2,4) != b'\x00\x00\x00\x0b':
                break
            params.append(val_byte)
            local_cursor += 6  # 0e val 00 00 00 0b
        subrecords.append({
            'offset': ABS + cursor,
            'has_start_marker': cluster_has_marker,
            'value': value,
            'params': params
        })
        cursor = local_cursor
        # Stop if the next bytes look like next block header
        if peek(data,cursor,4) == b'\x25\x01\x52\x0e' or peek(data,cursor,3) == b'\x01\x52\x0e':
            break
        # Failsafe to avoid infinite loop
        if next_blk_rel >=0 and cursor >= next_blk_rel:
            break
    blocks.append({
        'offset': block_start_abs,
        'has_start_marker': has_start_marker,
        'block_value': block_value,
        'subheader_index': subheader_index,
        'subrecord_count': len(subrecords),
        'subrecords': subrecords
    })
    # Move to next block start
    next_rel, next_marker = scan_next_block_start(data, cursor)
    if next_rel < 0:
        break
    pos = next_rel

print(json.dumps({
    'pattern_b_block_count': len(blocks),
    'blocks': blocks
}, indent=2))
