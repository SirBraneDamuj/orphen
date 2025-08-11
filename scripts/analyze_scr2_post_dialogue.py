#!/usr/bin/env python3
"""Structural analyzer for scr2.out content beyond dialogue region.

Goals:
 1. Identify pointer table length and map each entry to a dialogue line signature.
 2. Determine if post-table script references lines by index or by absolute offset.
 3. Segment remainder into blocks using heuristics (recurring patterns, delimiters).
 4. Produce frequency stats for token bytes (single + 2-byte sequences) excluding raw text.
 5. Attempt to discover how higher-level script references dialogue lines:
     a. Direct absolute offsets (4-byte or 2-byte?)
     b. Index values (single-byte 0..N-1)
 6. Enumerate repeating 0x5A 0x0C header-like patterns to infer fixed-length record structure.

Assumptions:
  - Dialogue region ends at 0x1680 (exclusive). Post region begins with pointer table of ascending uint32 (<0x1680).
  - Table terminated when ascending order breaks (next value <= previous) or value == 0.

Outputs:
  - JSON summary to stdout (or file with --json-out) containing: pointer_table, line_signatures, blocks, byte_frequencies.
  - Optional CSV of n-gram frequencies.

Heuristics for line_signature:
  - At pointed offset, expect optional 0x13 name record: 0x13, ascii name, 0x00 terminator.
  - Then expect 0x17 (control) then 0x16 voice packet (len=5) then 0x?? (sequence id) etc.
    - Extract: speaker, voice_mode, voice_id. (Previous speculative seq_id removed; data after voice packet often begins directly with control opcode 0x17 so prior field was misleading.)

Block segmentation heuristic (post pointer table remainder):
  - Treat 0x5A, 0x4F, 0x51 as potential opcode bytes; 0x0B as probable separator; 0x17 00 00 00 may delimit logical groups.
  - Start a new block after seeing long run of pattern (0x17 00 00 00) or after fixed max size threshold (e.g., 0x80) if no delimiter.

This is exploratory: results should be reviewed manually.
"""
import argparse, json, collections, os, struct, sys

DIALOGUE_END = 0x1680

def read_u32(data, off):
    if off+4>len(data): return None
    return struct.unpack_from('<I', data, off)[0]

def load_file(path):
    with open(path,'rb') as f: return f.read()

def extract_pointer_table(data):
    ptrs=[]
    off=DIALOGUE_END
    last=-1
    while True:
        v=read_u32(data, off)
        if v is None: break
        if not (v < DIALOGUE_END and v>last):
            # break if non-ascending or points outside dialogue region
            break
        ptrs.append(v)
        last=v
        off+=4
    table_end=off
    return ptrs, table_end

def parse_line_signature(data, off):
    """Parse a dialogue line header at a pointer-table target.

    We intentionally do NOT capture a post-voice 32-bit value because inspection shows
    the next byte following the 5-byte 0x16 packet is usually a control opcode (e.g., 0x17).
    """
    speaker=None
    voice_mode=None
    voice_id=None
    p=off
    if p < len(data) and data[p]==0x13:
        p+=1
        name_bytes=[]
        while p < len(data) and data[p] != 0x00 and len(name_bytes) < 96:
            name_bytes.append(data[p]); p+=1
        if p < len(data) and data[p]==0x00:
            p+=1
        speaker=bytes(name_bytes).decode('ascii','replace') if name_bytes else ''
    # scan for 0x16 voice opcode in next 24 bytes
    scan_range=data[p:p+32]
    idx=scan_range.find(b'\x16')
    voice_params=None
    corrected_voice_id=None
    if idx!=-1:
        # Expect opcode 0x16 + 6 parameter bytes (empirically observed):
        #   [0] mode (immediate/queue)
        #   [1] flag/unknown
        #   [2:4] little-endian voice id (2 bytes)
        #   [4:6] extra / unknown (2 bytes)
        # Some lines show padding zeros before next opcode 0x17.
        param_start=p+idx+1
        if param_start+6 <= len(data):
            voice_params=bytes(data[param_start:param_start+6])
            voice_mode=voice_params[0]
            raw_flag=voice_params[1]
            vid_le=voice_params[2] | (voice_params[3] << 8)
            corrected_voice_id=vid_le
            # Keep prior (incorrect) large voice_id legacy? We replace with corrected.
            voice_id=corrected_voice_id
    return {
        'offset': off,
        'speaker': speaker,
        'voice_mode': voice_mode,
        'voice_id': corrected_voice_id,
        'voice_params_hex': voice_params.hex() if voice_params else None,
    }

def tokenize_remainder(data, start):
    payload=data[start:]
    # Frequency counts skipping zero bytes maybe
    freq=collections.Counter(payload)
    # bigrams
    bigrams=collections.Counter(zip(payload, payload[1:]))
    return freq, bigrams

def segment_blocks(data, start):
    blocks=[]
    cur_start=start
    i=start
    while i < len(data):
        # delimiter if sequence 0x17 00 00 00 and current block has some length
        if i+4 <= len(data) and data[i]==0x17 and data[i+1]==0 and data[i+2]==0 and data[i+3]==0 and i>cur_start:
            blocks.append({'start':cur_start,'end':i,'size':i-cur_start})
            cur_start=i
            i+=4
            continue
        # size cap
        if i - cur_start >= 0x100:
            blocks.append({'start':cur_start,'end':i,'size':i-cur_start,'note':'size_cap'})
            cur_start=i
        i+=1
    if cur_start < len(data):
        blocks.append({'start':cur_start,'end':len(data),'size':len(data)-cur_start})
    return blocks

def scan_pointer_references(data, table_end, ptrs):
    """Heuristically scan Region C for references to dialogue lines.

    We look for:
      - Single-byte indices (0..len(ptrs)-1)
      - 2-byte little-endian values matching pointer offsets
      - 4-byte values matching pointer offsets
    Counts are approximate (no context disambiguation).
    """
    region=data[table_end:]
    index_hits=collections.Counter()
    offset16_hits=collections.Counter()
    offset32_hits=collections.Counter()
    ptr_set=set(ptrs)
    ptr16={p & 0xFFFF for p in ptrs}
    for i,b in enumerate(region):
        if b < len(ptrs):
            index_hits[b]+=1
        if i+1 < len(region):
            v16=region[i] | (region[i+1]<<8)
            if v16 in ptr16:
                offset16_hits[v16]+=1
        if i+3 < len(region):
            v32=region[i] | (region[i+1]<<8) | (region[i+2]<<16) | (region[i+3]<<24)
            if v32 in ptr_set:
                offset32_hits[v32]+=1
    return {
        'index_hits_top': index_hits.most_common(16),
        'offset16_hits_top': offset16_hits.most_common(16),
        'offset32_hits_top': [(hex(k),v) for k,v in offset32_hits.most_common(16)],
    }

def enumerate_5a0c_patterns(data, table_end):
    """Collect occurrences of 0x5A 0x0C and summarize trailing bytes.

    We treat each 0x5A 0x0C as a header; capture up to next 10 bytes for clustering.
    """
    region=data[table_end:]
    headers=[]
    contexts=[]  # (offset_in_file, tail_bytes, pre8, post8)
    i=0
    while i < len(region)-1:
        if region[i]==0x5A and region[i+1]==0x0C:
            tail=region[i+2:i+12]
            headers.append(bytes(tail))
            abs_off = table_end + i
            pre_start = max(0, abs_off-8)
            pre = data[pre_start:abs_off]
            post = data[abs_off+12:abs_off+12+8]
            contexts.append({
                'offset': abs_off,
                'pre_hex': pre.hex(),
                'tail_hex': tail.hex(),
                'post_hex': post.hex(),
            })
            i+=2
        else:
            i+=1
    cluster=collections.Counter(headers)
    top=[{'tail_hex':h.hex(), 'count':c} for h,c in cluster.most_common(48)]
    # Decompose into columns for heuristic structure inference
    cols=[collections.Counter() for _ in range(10)]
    pair_03=collections.Counter()  # (col0,col3)
    pair_37=collections.Counter()  # (col3,col7)
    for h in headers:
        for i,b in enumerate(h):
            cols[i][b]+=1
        pair_03[(h[0],h[3])] +=1
        pair_37[(h[3],h[7])] +=1
    col_stats=[{'index':i,'unique':len(c),'top':[(hex(b),n) for b,n in c.most_common(12)]} for i,c in enumerate(cols)]
    pair_03_top=[{'col0':hex(a),'col3':hex(b),'count':n} for (a,b),n in pair_03.most_common(20)]
    pair_37_top=[{'col3':hex(a),'col7':hex(b),'count':n} for (a,b),n in pair_37.most_common(20)]
    return {
        'count': len(headers),
        'unique': len(cluster),
        'top_examples': top,
        'columns': col_stats,
        'pairs_col0_col3': pair_03_top,
        'pairs_col3_col7': pair_37_top,
        'contexts': contexts[:64],
    }

def extract_small_id_list(data, table_end):
    """Extract the sequence of small 32-bit integers following the pointer table.

    Heuristic: after sentinel 0x00000000 and optional magic (non-small) value, read consecutive
    words where value <= 0x1000 (or <= 0xFF if we want stricter) until a gap of > 0x1000 or pattern break.
    Return list and end offset.
    """
    off = table_end
    if read_u32(data, off) == 0:
        off += 4
    # Optional magic (e.g., 0x00030e4d) keep but mark
    small_ids=[]
    magic=None
    first = read_u32(data, off)
    if first and first > 0x1000:
        magic = first
        off += 4
    # collect small values
    while True:
        v = read_u32(data, off)
        if v is None: break
        if v == 0: # allow zeros inside? break to avoid large zero padding sections
            break
        if v > 0x1000: # treat as end of small-id list
            break
        small_ids.append(v)
        off += 4
    return {
        'magic': magic,
        'ids': small_ids,
        'end_offset': off,
    }

def map_small_ids_to_5a0c(small_ids, pattern_5a0c):
    col0_vals=[int(entry['tail_hex'][0:2],16) for entry in pattern_5a0c.get('top_examples',[])[:pattern_5a0c['count']]]
    # Use contexts for full enumeration if available
    if 'contexts' in pattern_5a0c:
        col0_vals = [int(ctx['tail_hex'][0:2],16) for ctx in pattern_5a0c['contexts']]
    small_set=set(small_ids)
    overlap=[v for v in col0_vals if v in small_set]
    return {
        'small_id_count': len(small_ids),
        'col0_count': len(col0_vals),
        'overlap_count': len(overlap),
        'overlap_values': sorted(set(overlap)),
        'coverage_pct': (len(overlap)/len(col0_vals)*100.0) if col0_vals else 0.0,
    }

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--file', default='scr2.out')
    ap.add_argument('--json-out')
    ap.add_argument('--export-5a0c-csv')
    ap.add_argument('--limit', type=int, default=None)
    args=ap.parse_args()
    data=load_file(args.file)
    ptrs, table_end = extract_pointer_table(data)
    signatures=[parse_line_signature(data,p) for p in ptrs]
    freq,bigrams = tokenize_remainder(data, table_end)
    blocks = segment_blocks(data, table_end)
    ptr_ref_stats = scan_pointer_references(data, table_end, ptrs)
    pattern_5a0c = enumerate_5a0c_patterns(data, table_end)
    small_ids_info = extract_small_id_list(data, table_end)
    small_map = map_small_ids_to_5a0c(small_ids_info['ids'], pattern_5a0c)
    if args.limit:
        blocks=blocks[:args.limit]
    # Summaries
    top_freq=freq.most_common(32)
    top_bi=[({'b1':a,'b2':b,'count':c}) for (a,b),c in bigrams.most_common(32)]
    result={
        'pointer_table_count': len(ptrs),
        'pointer_table': ptrs,
        'table_end_offset': table_end,
        'line_signatures': signatures,
        'top_byte_frequencies': [(hex(b),c) for b,c in top_freq],
        'top_bigrams': top_bi,
        'block_count': len(blocks),
        'blocks': blocks[:50],  # truncate
        'pointer_reference_stats': ptr_ref_stats,
        'pattern_5a0c': pattern_5a0c,
        'small_id_list': small_ids_info,
        'small_id_vs_5a0c_col0': small_map,
    }
    # Optional CSV export for 5A0C entries
    if args.export_5a0c_csv and 'contexts' in pattern_5a0c:
        import csv
        with open(args.export_5a0c_csv,'w',newline='') as fcsv:
            w=csv.writer(fcsv)
            w.writerow(['offset','col0','col3','col7','col8','col9','tail_hex','pre_hex','post_hex'])
            for ctx in pattern_5a0c['contexts']:
                tail=ctx['tail_hex']
                # Ensure at least 20 hex chars
                col0=int(tail[0:2],16)
                col3=int(tail[6:8],16)
                col7=int(tail[14:16],16)
                col8=int(tail[16:18],16)
                col9=int(tail[18:20],16)
                w.writerow([hex(ctx['offset']), col0, col3, col7, col8, col9, tail, ctx['pre_hex'], ctx['post_hex']])
    out=json.dumps(result, indent=2)
    if args.json_out:
        with open(args.json_out,'w') as f: f.write(out)
    else:
        print(out)

if __name__=='__main__':
    main()
