"""Comprehensive post-pointer-region analyzer for Orphen SCR files.

Extracts:
  - Pattern A timing/duration records: 25 37 0e <ID> ... 0b <params> 0b
  - Pattern B hierarchical blocks:
        First block: 25 01 52 0e <u32>
        Subsequent:  01 52 0e <u32>
      With optional subheader (0b 0b 34/44 00 00 00 37 0e <u32> 0b 59 0b)
      And subrecords: [25] 79 0e <u32> 0b (param opcodes: 0e <val> 00 00 00 0b)
  - 0x04 ID records (script sub-process descriptors) heuristic:
        04 <id_lo> <id_hi> 04 00 00 [01] ... (capturing a fixed context slice)

Outputs JSON summarizing all parsed structures with offsets and raw hex.

Usage:
  python analyze_post_pointer_region.py scr2.out > scr2_post_pointer.json

Limitations:
  - Pattern B block end heuristic: next block header or end-of-region.
  - 0x04 ID record parsing is conservative (pattern-matched only if second 0x04 and two 0x00 follow).
  - Does not yet parse generic 5A 0C 12-byte action records; placeholder left for future extension.
"""
from __future__ import annotations
import sys, json, pathlib
from dataclasses import dataclass, asdict

HEADER_SIZE = 0x2C

@dataclass
class PatternARecord:
    offset: int
    id: int
    raw: str
    payload_hex: str
    params_hex: str

@dataclass
class PatternBSubrecord:
    offset: int
    has_leading_25: bool
    value_u32: int
    params: list[int]
    raw: str

@dataclass
class PatternBBlock:
    offset: int
    first_block: bool
    block_value: int
    subheader_type: int | None  # 0x34 or 0x44
    subheader_index: int | None
    subrecords: list[PatternBSubrecord]
    raw: str

@dataclass
class ID04Record:
    offset: int
    id16: int
    raw: str
    signature_class: str


def read_header(data: bytes):
    if len(data) < HEADER_SIZE:
        raise ValueError("File too small")
    words = [int.from_bytes(data[i:i+4], 'little') for i in range(0, HEADER_SIZE, 4)]
    return {
        'words': words,
        'dialogue_start': words[0],
        'dialogue_end': words[1],
        'pointer_table_start': words[5],
        'pointer_table_end': words[6],
        'footer_base': words[7],
    }


def parse_pattern_a(region: bytes, base_off: int):
    out = []
    i = 0
    while i < len(region)-6:
        if region[i:i+3] == b'\x25\x37\x0e':
            if i+4 >= len(region):
                break
            rec_start = i
            rec_id = region[i+3]
            # After ID, bytes until first 0x0b = header payload, then params until next 0x0b.
            j = i+4
            while j < len(region) and region[j] != 0x0b:
                j += 1
            if j >= len(region):
                break
            first_delim = j
            j += 1
            while j < len(region) and region[j] != 0x0b:
                j += 1
            if j >= len(region):
                break
            second_delim = j
            rec_end = second_delim + 1
            raw = region[rec_start:rec_end]
            out.append(PatternARecord(
                offset=base_off + rec_start,
                id=rec_id,
                raw=raw.hex(),
                payload_hex=region[rec_start+4:first_delim].hex(),
                params_hex=region[first_delim+1:second_delim].hex(),
            ))
            i = rec_end
        else:
            i += 1
    return out


def locate_pattern_b_headers(region: bytes):
    headers = []  # (offset, first_block, block_value)
    i = 0
    while i < len(region)-8:
        if region[i:i+4] == b'\x25\x01\x52\x0e':
            block_value = int.from_bytes(region[i+4:i+8], 'little')
            headers.append((i, True, block_value))
            i += 4
        elif region[i:i+3] == b'\x01\x52\x0e':
            block_value = int.from_bytes(region[i+3:i+7], 'little')
            headers.append((i, False, block_value))
            i += 3
        else:
            i += 1
    return headers


def parse_pattern_b(region: bytes, base_off: int):
    headers = locate_pattern_b_headers(region)
    out = []
    for idx, (local_off, first_block, block_value) in enumerate(headers):
        block_start = local_off
        block_end = headers[idx+1][0] if idx+1 < len(headers) else len(region)
        block_bytes = region[block_start:block_end]
        # Parse optional subheader inside block
        subheader_type = None
        subheader_index = None
        pos = 0
        if first_block:
            pos = 4 + 4  # 25 01 52 0e + u32
        else:
            pos = 3 + 4  # 01 52 0e + u32
        # Check for subheader signature
        if pos + 12 < len(block_bytes) and block_bytes[pos:pos+2] == b'\x0b\x0b' and block_bytes[pos+2] in (0x34,0x44):
            subheader_type = block_bytes[pos+2]
            if block_bytes[pos+3:pos+6] == b'\x00\x00\x00' and block_bytes[pos+6:pos+8] == b'\x37\x0e':
                subheader_index = int.from_bytes(block_bytes[pos+8:pos+12], 'little')
                # Expect sequence 0b 59 0b after that:
                # move pos past potential subheader (leave parsing of trailing bytes to subrecord loop)
            # Advance pos to after first 0b 59 0b if present
            # Find first occurrence of b'\x0b\x59\x0b' after index field
            marker_search = block_bytes.find(b'\x0b\x59\x0b', pos+12)
            if marker_search != -1:
                pos = marker_search + 3
        # Parse subrecords starting at current pos
        subrecords = []
        sr_pos = pos
        while sr_pos < len(block_bytes)-8:
            lead25 = False
            if block_bytes[sr_pos] == 0x25:
                lead25 = True
                sr_pos += 1
            if block_bytes[sr_pos:sr_pos+2] == b'\x79\x0e':
                val = int.from_bytes(block_bytes[sr_pos+2:sr_pos+6], 'little')
                after = sr_pos+6
                if after < len(block_bytes) and block_bytes[after] == 0x0b:
                    after += 1
                params = []
                # Collect param opcodes pattern: 0e <val> 00 00 00 0b
                while after+6 <= len(block_bytes) and block_bytes[after] == 0x0e and after+6 <= len(block_bytes):
                    pv = block_bytes[after+1]
                    if block_bytes[after+2:after+5] == b'\x00\x00\x00' and block_bytes[after+5] == 0x0b:
                        params.append(pv)
                        after += 6
                    else:
                        break
                raw = block_bytes[sr_pos - (1 if lead25 else 0):after]
                subrecords.append(PatternBSubrecord(
                    offset=base_off + block_start + (sr_pos - (1 if lead25 else 0)),
                    has_leading_25=lead25,
                    value_u32=val,
                    params=params,
                    raw=raw.hex(),
                ))
                sr_pos = after
            else:
                break  # stop parsing subrecords on first mismatch to avoid drifting into next block
        out.append(PatternBBlock(
            offset=base_off + block_start,
            first_block=first_block,
            block_value=block_value,
            subheader_type=subheader_type,
            subheader_index=subheader_index,
            subrecords=subrecords,
            raw=block_bytes.hex(),
        ))
    return out


def parse_id04(region: bytes, base_off: int):
    """Parse known/seeded ID04 records (tracked IDs only) with signature classification.

    We seed with IDs observed from runtime 'SCR SUBPROC' displays to focus on
    relevant subsets while we still explore full space separately.
    """
    out: list[ID04Record] = []
    # Tracked script ID 16-bit values (little-endian). Newly added as discovered: 0x1353 (4947),
    # 0x119C (4508), 0x0526 (1318), 0x0534 (1332), 0x053D (1341).
    TARGET_IDS = {0x04AA, 0x04B4, 0x11A7, 0x133F, 0x1353, 0x119C, 0x0526, 0x0534, 0x053D}
    sig_marker_a = b'\x9e\x0c\x01\x1e\x0b'
    i = 0
    while i < len(region)-4:
        if region[i] == 0x04:
            id16 = int.from_bytes(region[i+1:i+3], 'little')
            tail = region[i+3:i+8]
            plausible = len(tail) >= 1 and tail[0] in (0x04, 0x11, 0x00)
            if id16 in TARGET_IDS and plausible:
                # context slice
                start_ctx = i-16 if i >= 16 else 0
                end_ctx = i+32 if i+32 < len(region) else len(region)
                raw = region[start_ctx:end_ctx]
                # signature classification
                sig_class = 'OTHER'
                if i >= len(sig_marker_a) and region[i-len(sig_marker_a):i] == sig_marker_a:
                    sig_class = 'SIG_A_9e0c011e0b'
                else:
                    back_slice = region[i-16:i] if i >= 16 else region[0:i]
                    if b'\x0b\x0b\x02\x92\x0c' in back_slice:
                        sig_class = 'SIG_B_0b0b02920c'
                out.append(ID04Record(
                    offset=base_off + i,
                    id16=id16,
                    raw=raw.hex(),
                    signature_class=sig_class,
                ))
                i += 3
                continue
        i += 1
    return out


def enumerate_all_id04(region: bytes, base_off: int):
    """Enumerate ALL 0x04 <u16> occurrences with plausible tail; lightweight classification.

    Used to surface undiscovered IDs without needing to pre-seed TARGET_IDS.
    Signature classes:
      A => immediate preceding bytes match 9e0c011e0b
      B => within previous 16 bytes we see 0b0b02920c (alternate container style)
      UNK => none of the above
    """
    records = []
    sig_marker_a = b'\x9e\x0c\x01\x1e\x0b'
    i = 0
    while i < len(region)-4:
        if region[i] == 0x04:
            id16 = int.from_bytes(region[i+1:i+3], 'little')
            tail0 = region[i+3]
            if tail0 in (0x04, 0x11, 0x00):
                back = region[i-len(sig_marker_a):i] if i >= len(sig_marker_a) else b''
                sig = 'A' if back == sig_marker_a else 'B' if (b'\x0b\x0b\x02\x92\x0c' in region[i-16:i] if i >= 16 else False) else 'UNK'
                # Tightened filter: keep only if signature A/B OR immediate/following dword == 0x00000001
                keep = False
                has_dword1_at_3 = i+7 < len(region) and region[i+3:i+7] == b'\x00\x00\x00\x01'
                has_dword1_at_4 = i+8 < len(region) and region[i+4:i+8] == b'\x00\x00\x00\x01'
                if sig in ('A','B'):
                    keep = True  # always keep structured signatures
                else:
                    # For UNK, require a 0x00000001 literal AND a plausible preceding setup opcode byte
                    if (has_dword1_at_3 or has_dword1_at_4):
                        prev_byte = region[i-1] if i > 0 else 0
                        if prev_byte in (0x0b, 0x25, 0x37):  # delimiter or known structural lead-in
                            keep = True
                if keep:
                    start_ctx = i-8 if i >= 8 else 0
                    end_ctx = i+24 if i+24 < len(region) else len(region)
                    records.append({
                        'offset': base_off + i,
                        'id16': id16,
                        'signature_class': sig,
                        'context': region[start_ctx:end_ctx].hex(),
                    })
                    i += 3
                    continue
        i += 1
    return records


def main():
    if len(sys.argv) < 2:
        print("Usage: analyze_post_pointer_region.py <scr*.out>", file=sys.stderr)
        sys.exit(1)
    path = pathlib.Path(sys.argv[1])
    data = path.read_bytes()
    header = read_header(data)
    p_end = header['pointer_table_end']
    f_base = header['footer_base']
    if f_base > len(data):
        f_base = len(data)
    region = data[p_end:f_base]

    pattern_a = parse_pattern_a(region, p_end)
    pattern_b = parse_pattern_b(region, p_end)
    id04 = parse_id04(region, p_end)
    id04_all = enumerate_all_id04(region, p_end)
    sig_counts = {}
    for r in id04_all:
        sig_counts[r['signature_class']] = sig_counts.get(r['signature_class'], 0) + 1
    # Duplicate detection for expected-unique IDs (exclude id16==0 as noise)
    id_freq = {}
    for r in id04_all:
        if r['id16'] != 0:
            id_freq[r['id16']] = id_freq.get(r['id16'], 0) + 1
    duplicate_ids = {k:v for k,v in id_freq.items() if v > 1}
    # Unique ID summary (non-zero IDs): signature class distribution & first offset
    unique_summary = []
    for idv, cnt in sorted(id_freq.items()):
        class_counts = {}
        first_off = None
        for r in id04_all:
            if r['id16'] == idv:
                class_counts[r['signature_class']] = class_counts.get(r['signature_class'], 0) + 1
                if first_off is None:
                    first_off = r['offset']
        unique_summary.append({
            'id16': idv,
            'count': cnt,
            'signature_classes': class_counts,
            'first_offset': first_off,
        })

    result = {
        'file': str(path),
        'header': header,
        'counts': {
            'pattern_a': len(pattern_a),
            'pattern_b_blocks': len(pattern_b),
            'id04_records': len(id04),
            'id04_all_records': len(id04_all),
        },
        'pattern_a_records': [asdict(r) for r in pattern_a],
        'pattern_b_blocks': [
            {**asdict(b), 'subrecords': [asdict(sr) for sr in b.subrecords]} for b in pattern_b
        ],
        'id04_records': [asdict(r) for r in id04],
        'id04_all': id04_all,
        'id04_all_signature_counts': sig_counts,
    'id04_all_duplicate_ids': duplicate_ids,
    'id04_all_unique_ids': unique_summary,
    }
    json.dump(result, sys.stdout, indent=2)

if __name__ == '__main__':
    main()
