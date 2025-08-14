#!/usr/bin/env python3
"""
locate_script_interpreter_candidates.py

Goal: Rank decompiled C files (and coarse-grained functions) by likelihood of being the post-pointer
region script structure interpreter based on presence of key opcode / delimiter byte constants.

Heuristic Rationale:
  The interpreter that recognizes Signature A (9e 0c 01 1e 0b) and Signature B (0b 0b 02 92 0c)
  must reference multiple of these byte values. We look for constants among:
    0x0b (record delimiter), 0x0e (parameter introducer), 0x04 (subproc ID tag),
    0x25, 0x37, 0x01, 0x52 (pattern heads), 0x79 (subrecord start), 0x9e, 0x92 (signature bytes).

Approach:
  1. Scan each .c file under a root directory (default: ../src relative to this script location).
  2. Collect hex byte constants (regex 0x[0-9a-f]+) <= 0xFF in the file.
  3. Optionally (flag) include decimal constants (11,14,4, etc.) – off by default due to noise.
  4. Compute score = sum(weight[byte]) with tunable per-byte weights.
  5. Also compute distinct hit count, total occurrences for target bytes, and Jaccard similarity
     vs target set.
  6. Attempt a light function-level breakdown: treat any line whose leftmost token contains
     'FUN_' as a possible new function boundary and accumulate per-function stats.

Output:
  Human-readable table (default) sorted by descending score, then Jaccard, then path.
  Optional JSON (--json) for downstream tooling.

Future Enhancements (TODO):
  - Add pattern adjacency detection (e.g., two of the signature bytes appearing within N chars).
  - Track pointer increment idioms (ptr++ / p += 1) within candidate functions.
  - Compute rolling window substrings to see if signature subsequences occur even if not literal.

Usage:
  python locate_script_interpreter_candidates.py --root ../src --top 40
  python locate_script_interpreter_candidates.py --json > candidates.json

Exit code 0 always (diagnostic tool).
"""
from __future__ import annotations
import argparse, json, re, sys, pathlib, collections, math, dataclasses

TARGET_BYTES = {
    0x0b, 0x0e, 0x04, 0x25, 0x37, 0x01, 0x52, 0x79, 0x9e, 0x92
}
# Weights emphasize rarer signature bytes & structural delimiters
WEIGHTS = {
    0x0b: 2.0,  # delimiter
    0x0e: 2.0,  # parameter introducer
    0x04: 1.5,  # ID tag
    0x25: 1.0,
    0x37: 1.0,
    0x01: 0.5,
    0x52: 1.0,
    0x79: 1.5,
    0x9e: 2.5,
    0x92: 2.5,
}
HEX_CONST_RE = re.compile(r"0x([0-9a-fA-F]+)")
# Hex constants not immediately used as an index like [0x52]
HEX_NON_INDEX_RE = re.compile(r"(?<!\[)\b0x([0-9a-fA-F]{1,2})\b")
# Lines where constant participates in a comparison or switch (==, !=, case )
COMPARISON_LINE_RE = re.compile(r"(==|!=|case)\s*0x[0-9a-fA-F]{1,2}")
FUNC_BOUNDARY_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_\s\*]+)?(FUN_[0-9a-fA-F]+)\\s*\(")
DECIMAL_BYTES = {11,14,4,0x25,0x37,1,0x52,0x79,0x9e,0x92}
DEC_CONST_RE = re.compile(r"(?<![0-9])(\d{1,3})(?![0-9])")  # crude

def parse_args():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=str(pathlib.Path(__file__).resolve().parent.parent / 'src'), help='Root directory to scan (default: ../src relative to script).')
    ap.add_argument('--top', type=int, default=30, help='Show top N results.')
    ap.add_argument('--json', action='store_true', help='Emit JSON instead of table.')
    ap.add_argument('--include-dec', action='store_true', help='Also consider small decimal constants (noise-prone).')
    ap.add_argument('--min-distinct', type=int, default=3, help='Minimum distinct target bytes required to list a scope.')
    ap.add_argument('--comparisons-only', action='store_true', help='Only count constants that appear in comparison/switch contexts.')
    return ap.parse_args()

# (Reserved for potential future structured stat objects – currently using plain dicts.)

class FunctionAccumulator:
    def __init__(self, name: str):
        self.name = name
        self.counts = collections.Counter()
    def add_byte(self, b: int):
        if b in TARGET_BYTES:
            self.counts[b] += 1
    def finalize(self, path: str):
        if not self.counts:
            return None
        bytes_present = set(self.counts)
        distinct_hits = len(bytes_present)
        total_occurrences = sum(self.counts.values())
        score = sum(WEIGHTS[b] * self.counts[b] for b in self.counts)
        jaccard = distinct_hits / len(TARGET_BYTES)
        return {
            'path': path,
            'scope': self.name,
            'score': score,
            'distinct_hits': distinct_hits,
            'total_occurrences': total_occurrences,
            'bytes_present': sorted(hex(b) for b in bytes_present),
            'jaccard': jaccard,
            'counts': {hex(k): v for k, v in sorted(self.counts.items())},
        }

def scan_file(path: pathlib.Path, include_dec: bool, comparisons_only: bool):
    try:
        text = path.read_text(encoding='utf-8', errors='ignore')
    except Exception:
        return [], []
    file_counts = collections.Counter()
    # Hex constants
    if comparisons_only:
        # restrict to lines containing a comparison context
        for line in text.splitlines():
            if COMPARISON_LINE_RE.search(line):
                for m in HEX_NON_INDEX_RE.finditer(line):
                    val = int(m.group(1), 16)
                    if val in TARGET_BYTES:
                        file_counts[val] += 1
    else:
        for m in HEX_NON_INDEX_RE.finditer(text):
            val = int(m.group(1), 16)
            if val in TARGET_BYTES:
                file_counts[val] += 1
    # Optional decimal constants
    if include_dec:
        for m in DEC_CONST_RE.finditer(text):
            val = int(m.group(1))
            if val <= 0xFF and val in TARGET_BYTES:
                file_counts[val] += 1
    # Attempt light function breakdown
    function_results = []
    current = FunctionAccumulator('__file__')
    lines = text.splitlines()
    for line in lines:
        # Detect function boundary (simple heuristic)
        if 'FUN_' in line and '(' in line and '{' in line.split('//')[0]:
            # finalize previous
            res = current.finalize(str(path))
            if res:
                function_results.append(res)
            # start new
            fun_name_match = re.search(r'(FUN_[0-9a-fA-F]+)', line)
            fun_name = fun_name_match.group(1) if fun_name_match else 'FUN_???'
            current = FunctionAccumulator(fun_name)
        # Find constants in line
        consider_line = True
        if comparisons_only and not COMPARISON_LINE_RE.search(line):
            consider_line = False
        if consider_line:
            for m in HEX_NON_INDEX_RE.finditer(line):
                val = int(m.group(1), 16)
                current.add_byte(val)
            if include_dec:
                for m in DEC_CONST_RE.finditer(line):
                    val = int(m.group(1))
                    current.add_byte(val)
    # finalize last
    res = current.finalize(str(path))
    if res:
        function_results.append(res)
    # Build file-level result
    if file_counts:
        distinct = len(file_counts)
        total = sum(file_counts.values())
        score = sum(WEIGHTS[b] * file_counts[b] for b in file_counts)
        jaccard = distinct / len(TARGET_BYTES)
        file_result = {
            'path': str(path),
            'scope': '__file__',
            'score': score,
            'distinct_hits': distinct,
            'total_occurrences': total,
            'bytes_present': sorted(hex(b) for b in file_counts),
            'jaccard': jaccard,
            'counts': {hex(k): v for k, v in sorted(file_counts.items())},
        }
    else:
        file_result = None
    return function_results, ([file_result] if file_result else [])


def main():
    args = parse_args()
    root = pathlib.Path(args.root).resolve()
    if not root.exists():
        print(f"Root not found: {root}", file=sys.stderr)
        return 1
    all_results = []
    file_results = []
    for p in root.rglob('*.c'):
        f_funcs, f_file = scan_file(p, include_dec=args.include_dec, comparisons_only=args.comparisons_only)
        all_results.extend(f_funcs)
        file_results.extend(f_file)
    # Merge function + file lists for a unified ranking viewpoint
    combined = all_results + file_results
    # Diversity weighting: multiply score by (1 + distinct_hits/len(TARGET_BYTES))
    for r in combined:
        r['adj_score'] = r['score'] * (1.0 + r['distinct_hits']/len(TARGET_BYTES) if r['distinct_hits'] else 1.0)
    combined = [r for r in combined if r['distinct_hits'] >= args.min_distinct]
    combined.sort(key=lambda r: (-r['adj_score'], -r['distinct_hits'], -r['jaccard'], r['path'], r['scope']))
    if args.json:
        out = {
            'target_bytes': [hex(b) for b in sorted(TARGET_BYTES)],
            'weights': {hex(k): v for k, v in WEIGHTS.items()},
            'results': combined[:args.top]
        }
        print(json.dumps(out, indent=2))
    else:
        print("# Script Interpreter Candidate Ranking (refined)")
        print("# target bytes:", ', '.join(hex(b) for b in sorted(TARGET_BYTES)))
        print(f"# root: {root}\n")
        header = f"{'ASCORE':>7} {'RAW':>6} {'DH':>2} {'TOT':>3}  {'SCOPE':<20} PATH  BYTES"
        print(header)
        print('-'*len(header))
        for r in combined[:args.top]:
            print(f"{r['adj_score']:7.2f} {r['score']:6.2f} {r['distinct_hits']:2d} {r['total_occurrences']:3d}  {r['scope']:<20} {r['path']}  {','.join(r['bytes_present'])}")
    return 0

if __name__ == '__main__':
    sys.exit(main())
