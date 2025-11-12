#!/usr/bin/env python3
"""
Probe D-record shorts at offsets 24 and 26 to hypothesize which holds the A-index.
Reports how many values fall within [0, len(A)) and basic histograms.
"""
import argparse, json
from psm2_parser import parse_psm2, MAGIC_PSM2

def find_psm2_offsets(buf: bytes) -> list[int]:
    magic = MAGIC_PSM2.to_bytes(4, 'little')
    offs = []
    pos = 0
    while True:
        i = buf.find(magic, pos)
        if i == -1:
            break
        if buf[i:i+4] == magic:
            offs.append(i)
        pos = i + 1
    return offs


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('input')
    args = ap.parse_args(argv)
    data = open(args.input,'rb').read()
    from psm2_parser import _u32, _u16, _s16
    # find first PSM2
    magic = MAGIC_PSM2.to_bytes(4,'little')
    o = data.find(magic)
    if o < 0:
        raise SystemExit('no PSM2')
    buf = data[o:]
    offs_d = _u32(buf, 0x0C)
    offs_a = _u32(buf, 0x04)
    a_cnt = _s16(buf, offs_a) if offs_a else 0
    base = offs_d
    cnt = _s16(buf, base)
    p = base + 2
    vals24 = []
    vals26 = []
    for i in range(max(0,cnt)):
        if p + 32 > len(buf):
            break
        vals24.append(_u16(buf, p+24))
        vals26.append(_u16(buf, p+26))
        p += 32
    def within(lst):
        return sum(1 for v in lst if 0 <= v < a_cnt)
    from collections import Counter
    print(json.dumps({
        'A_count': a_cnt,
        'D_count': cnt,
        'offset24_within_A': within(vals24),
        'offset26_within_A': within(vals26),
        'offset24_top': Counter(vals24).most_common(10),
        'offset26_top': Counter(vals26).most_common(10),
    }, indent=2))
    return 0

if __name__ == '__main__':
    import sys
    raise SystemExit(main(sys.argv[1:]))
