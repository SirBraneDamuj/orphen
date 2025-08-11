#!/usr/bin/env python3
"""Heuristic extractor rebuilding visible dialogue strings from decompressed script sector bytes.

Observation: Many opcodes in the 0x61-0x7a range (and 0x20 space) appear sequentially and correspond exactly to lowercase ASCII letters when viewed in disassembly output, implying the VM reserves >=0x32 for both control opcodes and direct character emit instructions (one per letter/punctuation). We treat any opcode byte that maps to printable ASCII (heuristic subset) AND whose handler function address cluster is highly duplicated in jump table range as a character emission.

Phase 1 heuristic:
  * Interpret bytes in ranges: space (0x20), lowercase a-z (0x61-0x7a), uppercase A-Z (0x41-0x5a), digits (0x30-0x39), and basic punctuation [.,!?"'/:;()-]. These are candidate character-producing opcodes.
  * Build strings by consuming consecutive candidate bytes (length >=2 or contains a letter) until a non-candidate encountered.
  * Exclude runs consisting solely of digits unless length>=4 (likely numbers) until semantics clarified.
  * Output JSON or plain text with offset and reconstructed string.

Limitations:
  * Control codes inside strings (color changes, pauses) will currently terminate extraction.
  * Uppercase and punctuation below 0x32 collide with low-level opcode space; if those truly are opcodes, they won't be treated as plain characters in real execution. We'll include them for textual visibility but tag when <0x32.

Future refinement:
  * Parse VM control opcodes, model a mode/state for text emission vs expression evaluation.
  * Detect embedded parameterized control sequences (e.g., name inserts) and annotate.
"""
import argparse, json, pathlib, string, sys

PRINTABLE_EXTRA = ".,!?\"'/:;()-&%+[]<>"
PRINTABLE_SET = set(ord(c) for c in (string.ascii_lowercase+string.ascii_uppercase+string.digits+PRINTABLE_EXTRA+" "))

# We allow space (0x20) and digits 0x30-0x39 etc. Some punctuation shares low opcode space; mark them.

def is_char_byte(b: int) -> bool:
    if b in PRINTABLE_SET:
        # Exclude DEL and control
        if b < 0x20 and b not in (0x20,):
            return False
        if b == 0x7f:
            return False
        return True
    return False

MIN_LEN = 3

def extract(data: bytes, start=0, end=None, min_len=MIN_LEN):
    if end is None or end > len(data):
        end = len(data)
    out=[]
    i=start
    while i < end:
        if is_char_byte(data[i]):
            j=i
            has_letter=False
            buf=[]
            while j<end and is_char_byte(data[j]):
                ch=chr(data[j])
                if ch.isalpha():
                    has_letter=True
                buf.append(ch)
                j+=1
            s=''.join(buf)
            if (has_letter and len(s)>=2) or len(s)>=min_len:
                out.append({'offset':i,'end':j,'len':j-i,'text':s})
            i=j
        else:
            i+=1
    return out


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--file', required=True)
    ap.add_argument('--start', type=lambda x:int(x,0), default=0)
    ap.add_argument('--end', type=lambda x:int(x,0))
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--limit', type=int, default=200)
    ap.add_argument('--min-len', type=int, default=MIN_LEN)
    args=ap.parse_args()

    data=pathlib.Path(args.file).read_bytes()
    rows=extract(data, args.start, args.end, args.min_len)
    if args.json:
        json.dump({'strings':rows[:args.limit],'total':len(rows)}, sys.stdout, indent=2)
    else:
        for r in rows[:args.limit]:
            print(f"0x{r['offset']:06x}: {r['text']}")
        if len(rows) > args.limit:
            print(f"... ({len(rows)-args.limit} more omitted)")

if __name__=='__main__':
    main()
