"""Opcode statistical analyzer for post-pointer region of Orphen SCR files.

Derives quantitative support for inferred grammar elements:
  - Leading 0x25 record starter (Pattern A / first Pattern B block / subrecord optional prefix)
  - Delimiter 0x0b field/record separators
  - Parameter opcode 0x0e and its following value byte
  - Subrecord marker 0x79 0x0e <u32>
  - Prologue signature for ID04 regions: 9e 0c 01 1e 0b

Outputs:
  * overall_byte_frequency: count of each byte value encountered in region
  * following_byte_frequency: for selected lead bytes {0x25,0x0b,0x0e,0x79} a histogram of the immediate next byte
  * bigram_top: top-N most common bigrams (default 40)
  * trigram_signature_counts: counts of specific trigrams of interest (e.g., 25 37 0e, 25 01 52, 79 0e <xx>)
  * prologue_count_9e0c011e0b: number of exact occurrences of the 5-byte prologue
  * prologue_offsets (optional, limited) first K offsets of the prologue (default 32 to keep JSON small)
  * param_value_frequency: distribution of the second byte in 0e <val> 00 00 00 0b pattern

Usage:
    # Print full JSON to stdout (current behaviour)
    python analyze_opcode_stats.py scr2.out > scr2_opcode_stats.json

    # Write full JSON and a condensed summary side-by-side (no large stdout spam)
    python analyze_opcode_stats.py scr2.out --write-full scr2_opcode_stats.json --write-summary scr2_opcode_stats_summary.json

Summary file contents are a compact subset for quick iteration: key opcode counts, leading-byte follow histograms (top N), param value top N, prologue count, and selected bigrams.

Notes:
  - Analysis restricted to bytes between pointer_table_end and footer_base per earlier structural deductions.
  - Does not attempt to parse semantic records; purely statistical to guide further reverse engineering.
"""
from __future__ import annotations
import sys, json, pathlib, collections
from typing import Sequence

HEADER_SIZE = 0x2C

SELECT_LEADS = [0x25, 0x0b, 0x0e, 0x79]
PROLOGUE = b"\x9e\x0c\x01\x1e\x0b"


def read_header(data: bytes):
    if len(data) < HEADER_SIZE:
        raise ValueError("File too small")
    words = [int.from_bytes(data[i:i+4], 'little') for i in range(0, HEADER_SIZE, 4)]
    return {
        'words': words,
        'pointer_table_start': words[5],
        'pointer_table_end': words[6],
        'footer_base': words[7],
    }


def _arg_positions(args: Sequence[str], flag: str):
    try:
        i = args.index(flag)
        return i
    except ValueError:
        return -1


def main():
    if len(sys.argv) < 2 or sys.argv[1].startswith('-'):
        print("Usage: analyze_opcode_stats.py <scr*.out> [--write-full <path>] [--write-summary <path>]", file=sys.stderr)
        sys.exit(1)
    argv = sys.argv[1:]
    path = pathlib.Path(argv[0])
    write_full = None
    write_summary = None
    if '--write-full' in argv:
        idx = _arg_positions(argv, '--write-full')
        try:
            write_full = pathlib.Path(argv[idx+1])
        except IndexError:
            print("--write-full requires a path", file=sys.stderr)
            sys.exit(2)
    if '--write-summary' in argv:
        idx = _arg_positions(argv, '--write-summary')
        try:
            write_summary = pathlib.Path(argv[idx+1])
        except IndexError:
            print("--write-summary requires a path", file=sys.stderr)
            sys.exit(2)
    data = path.read_bytes()
    header = read_header(data)
    p_end = header['pointer_table_end']
    f_base = min(header['footer_base'], len(data))
    region = data[p_end:f_base]

    overall = [0]*256
    follow = {b: collections.Counter() for b in SELECT_LEADS}
    bigrams = collections.Counter()
    trigram_interest = {
        b"\x25\x37\x0e": 0,
        b"\x25\x01\x52": 0,
        b"\x01\x52\x0e": 0,  # subsequent pattern B blocks start
        b"\x79\x0e": 0,        # subrecord prefix (we'll treat this as a 2-byte signature)
    }
    prologue_offsets = []
    prologue_count = 0
    param_value_freq = collections.Counter()

    n = len(region)
    for i, b in enumerate(region):
        overall[b] += 1
        if i+1 < n:
            bigrams[region[i:i+2]] += 1
        # lead following byte frequency
        if b in follow and i+1 < n:
            follow[b][region[i+1]] += 1
        # trigram signatures
        for sig in list(trigram_interest.keys()):
            if len(sig) == 3 and i+3 <= n and region[i:i+3] == sig:
                trigram_interest[sig] += 1
        # 2-byte signature counting (79 0e) treat as pseudo-trigram interest (already above for len 2)
        if region[i:i+2] == b"\x79\x0e":
            trigram_interest[b"\x79\x0e"] += 1
        # prologue detection
        if i+len(PROLOGUE) <= n and region[i:i+5] == PROLOGUE:
            prologue_count += 1
            if len(prologue_offsets) < 32:
                prologue_offsets.append(p_end + i)
        # parameter opcode pattern 0e <val> 00 00 00 0b
        if b == 0x0e and i+6 <= n and region[i+2:i+5] == b"\x00\x00\x00" and region[i+5] == 0x0b:
            param_value_freq[region[i+1]] += 1

    # Prepare JSON-friendly structures
    overall_freq = {f"{i:02x}": c for i, c in enumerate(overall) if c}
    follow_freq = {f"{lead:02x}": {f"{k:02x}": v for k, v in cnt.most_common()} for lead, cnt in follow.items() if sum(cnt.values())}
    bigram_top = [
        {"bigram": bytes(k).hex(), "count": v}
        for k, v in bigrams.most_common(40)
    ]
    trigram_counts_out = {k.hex(): v for k, v in trigram_interest.items()}
    param_value_out = {f"{k:02x}": v for k, v in param_value_freq.most_common()}

    result = {
        'file': str(path),
        'header': header,
        'region_size': len(region),
        'overall_byte_frequency_count': len(overall_freq),
        'overall_byte_frequency': overall_freq,
        'following_byte_frequency': follow_freq,
        'bigram_top40': bigram_top,
        'trigram_signature_counts': trigram_counts_out,
        'prologue_count_9e0c011e0b': prologue_count,
        'prologue_offsets_sample': prologue_offsets,
        'param_value_frequency': param_value_out,
    }
    # Optional condensed summary (keeps iteration fast)
    if write_summary:
        # Select interesting opcodes for quick glance
        interesting = ['0b','0e','25','36','37','79','9e','1e','04']
        op_counts = {k: overall_freq.get(k, 0) for k in interesting}
        # Trim following-byte frequency to top 16 per lead for summary
        follow_trim = {}
        for lead_hex, dist in follow_freq.items():
            items = list(dist.items())[:16]
            follow_trim[lead_hex] = dict(items)
        summary = {
            'file': str(path),
            'region_size': len(region),
            'prologue_count_9e0c011e0b': prologue_count,
            'opcode_counts_subset': op_counts,
            'following_byte_frequency_top16': follow_trim,
            'trigram_signature_counts': trigram_counts_out,
            'param_value_frequency_top32': {k:v for k,v in list(param_value_out.items())[:32]},
            'bigram_selected': {k['bigram']: k['count'] for k in bigram_top if k['bigram'] in {'0b0e','0b0b','1e0b','011e','9e0c','0e00','0b04','0b78','7736','7836'}},
        }
        write_summary.write_text(json.dumps(summary, indent=2))
    # Decide output target for full
    if write_full:
        write_full.write_text(json.dumps(result, indent=2))
    else:
        json.dump(result, sys.stdout, indent=2)

if __name__ == '__main__':
    main()
