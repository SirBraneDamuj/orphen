#!/usr/bin/env python3
"""
Heuristically probe PSC3 chunks for geometry-like sections.

For each PSC3 file in out/maps ('.dec.bin'), it will:
- Read first 0x200 bytes, parse dwords
- Treat dword[4] as header size when plausible (0x40..0x80 typical), otherwise infer as min nonzero offset
- Collect candidate section offsets from dword[2..15]
- For each section, scan for long runs of 12-byte float triplets (x,y,z) with reasonable ranges
  and report the longest run length and bounding box.
- Optionally check for 16-bit index-like sequences (values < 65535) and their distribution.

Outputs a summary table with index, header size guess, top vertex-like run length and bbox.
"""
from __future__ import annotations

import argparse
import os
import re
import struct
from math import isfinite

OUT_DIR = 'out/maps'


def read(path: str) -> bytes:
    with open(path, 'rb') as f:
        return f.read()


def dwords_le(buf: bytes, n=32):
    return [struct.unpack_from('<I', buf, 4*i)[0] for i in range(min(n, len(buf)//4))]


def as_f32(b: bytes, off: int) -> float:
    return struct.unpack_from('<f', b, off)[0]


def find_vertex_runs(b: bytes, start: int, max_scan: int = 1<<20):
    """Scan for runs of xyz triplets; return best (count, bbox)."""
    end = min(len(b), start + max_scan)
    best = (0, (0,0,0,0,0,0))
    i = start
    current = 0
    minx=miny=minz=1e30
    maxx=maxy=maxz=-1e30
    def reset():
        nonlocal current, minx,miny,minz,maxx,maxy,maxz
        current = 0
        minx=miny=minz=1e30
        maxx=maxy=maxz=-1e30
    def push(x,y,z):
        nonlocal current, minx,miny,minz,maxx,maxy,maxz,best
        current += 1
        minx=min(minx,x); miny=min(miny,y); minz=min(minz,z)
        maxx=max(maxx,x); maxy=max(maxy,y); maxz=max(maxz,z)
        if current>best[0]:
            best=(current,(minx,miny,minz,maxx,maxy,maxz))
    reset()
    while i+12<=end:
        x=as_f32(b,i); y=as_f32(b,i+4); z=as_f32(b,i+8)
        if all(isfinite(v) and -1e6<=v<=1e6 for v in (x,y,z)):
            push(x,y,z); i+=12
        else:
            reset(); i+=4 # realign to next dword boundary and try again
            i = (i+3)&~3
    return best


def main(argv=None)->int:
    ap=argparse.ArgumentParser()
    ap.add_argument('--limit',type=int,default=15)
    args=ap.parse_args(argv)

    rx=re.compile(r'^map_(\d{4})\.dec\.bin$',re.I)
    files=[f for f in os.listdir(OUT_DIR) if rx.match(f)]
    files.sort()
    print('idx  hdr  bestRun  bbox')
    printed=0
    m=None
    for name in files:
        if printed>=args.limit: break
        p=os.path.join(OUT_DIR,name)
        b=read(p)
        if len(b)<0x100: continue
        dws=dwords_le(b, 24)
        if dws[0]!=0x33435350: # PSC3
            continue
        hdr = dws[4] if 0x20<=dws[4]<=0x200 else min([x for x in dws[2:16] if x>0] or [0x44])
        # collect plausible section offsets
        offs=[x for x in dws[2:16] if x and x<len(b)]
        offs=sorted(set(offs))
        best=(0,(0,0,0,0,0,0)); where=0
        for off in offs:
            count,bbox=find_vertex_runs(b, off)
            if count>best[0]:
                best=(count,bbox); where=off
        m=rx.match(name)
        idx=m.group(1) if m else '????'
        print(f"{idx}  0x{hdr:02x}  {best[0]:6d}  {tuple(round(v,3) for v in best[1])} @0x{where:x}")
        printed+=1
    return 0

if __name__=='__main__':
    import sys
    raise SystemExit(main(sys.argv[1:]))
