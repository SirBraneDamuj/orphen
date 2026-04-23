#!/usr/bin/env python3
"""
Export decoded PSC3 chunks to OBJ using code-backed section layout.

Replaces prior heuristics with explicit parsing of:
- +0x14 vertex record table (stride 10 bytes: x,y,z (s16/2048), float4 index (u16), extra u16 TBD)
- +0x18 vertex byte table (ignored for geometry; available for future attribute export)
- +0x20 color table (3 bytes per vertex; optional)
- +0x28 float4 table (normals/tangents: xyz used)

Faces: provisional sequential triangles (0,1,2),(3,4,5)... until exhausted; refined indexing TBD.
If vertex count not divisible by 3, trailing verts are emitted without faces.
"""
from __future__ import annotations
import argparse
import os
import re
import struct
from typing import List, Tuple
import traceback

PSC3_MAGIC = 0x33435350
SRC_DIR = 'out/maps'
DST_DIR = 'out/obj_real'


class PSC3:
    def __init__(self, buf: bytes, base_off: int = 0):
        self.buf = buf[base_off:]
        self.base_off = base_off
        if len(self.buf) < 0x50:
            raise ValueError('buffer too small')
        self.magic = struct.unpack_from('<I', self.buf, 0x00)[0]
        if self.magic != PSC3_MAGIC:
            raise ValueError('not PSC3')
        self.submesh_count = struct.unpack_from('<H', self.buf, 0x04)[0]
        # offsets
        self.offs_submesh_list = struct.unpack_from('<I', self.buf, 0x08)[0]
        self.offs_vertex_records = struct.unpack_from('<I', self.buf, 0x14)[0]
        self.offs_vertex_byte_table = struct.unpack_from('<I', self.buf, 0x18)[0]
        self.offs_draw_desc = struct.unpack_from('<I', self.buf, 0x1C)[0]
        self.offs_color_table = struct.unpack_from('<I', self.buf, 0x20)[0]
        self.offs_resource_table = struct.unpack_from('<I', self.buf, 0x24)[0]
        self.offs_float4_table = struct.unpack_from('<I', self.buf, 0x28)[0]

    def _slice(self, off: int, size: int) -> bytes:
        if off == 0 or size <= 0:
            return b''
        end = off + size
        if end > len(self.buf):
            raise ValueError('slice OOB')
        return self.buf[off:end]

    def parse_vertex_records(self) -> List[Tuple[float, float, float, int, int]]:
        off = self.offs_vertex_records
        if off == 0:
            return []
        # Heuristic: stop at smallest next section offset
        next_candidates = [x for x in [self.offs_vertex_byte_table, self.offs_draw_desc, self.offs_color_table, self.offs_resource_table, self.offs_float4_table] if x and x > off]
        end = min(next_candidates) if next_candidates else len(self.buf)
        stride = 10
        count = (end - off) // stride
        out = []
        for i in range(count):
            rec = self._slice(off + i*stride, stride)
            x, y, z, idx_float4, extra = struct.unpack_from('<hhhHH', rec, 0)
            out.append((x/2048.0, y/2048.0, z/2048.0, idx_float4, extra))
        return out

    def parse_float4_table(self) -> List[Tuple[float, float, float, float]]:
        off = self.offs_float4_table
        if off == 0:
            return []
        next_candidates = [x for x in [self.offs_submesh_list, self.offs_vertex_records, self.offs_vertex_byte_table, self.offs_draw_desc, self.offs_color_table, self.offs_resource_table] if x and x > off]
        end = min(next_candidates) if next_candidates else len(self.buf)
        stride = 16
        count = (end - off)//stride
        out = []
        for i in range(count):
            rec = self._slice(off + i*stride, stride)
            vx,vy,vz,vw = struct.unpack_from('<ffff', rec, 0)
            out.append((vx,vy,vz,vw))
        return out

    def parse_color_table(self) -> List[Tuple[int, int, int]]:
        off = self.offs_color_table
        if off == 0:
            return []
        # assume 3 bytes per vertex until next section
        next_candidates = [x for x in [self.offs_resource_table, self.offs_float4_table] if x and x > off]
        end = min(next_candidates) if next_candidates else len(self.buf)
        stride = 3
        count = (end - off)//stride
        out = []
        for i in range(count):
            rec = self._slice(off + i*stride, stride)
            r,g,b = rec
            out.append((r,g,b))
        return out

    def parse_draw_descs(self) -> List[dict]:
        """Parse draw descriptor table entries.
        Layout (per FUN_00212058/FUN_002129b8):
          +0x00..0x06: 4 x int16 vertex indices (i0,i1,i2,i3)
          +0x08: u16 flags
          +0x0C: u8 unk (used as mask)
          +0x0E..0x14: 4 x int16 resource stream indices (last non -1 preferred)
          +0x16: u16 float4 index when (flags & 8) == 0
        """
        off = self.offs_draw_desc
        if off == 0:
            return []
        next_candidates = [x for x in [self.offs_color_table, self.offs_resource_table, self.offs_float4_table] if x and x > off]
        end = min(next_candidates) if next_candidates else len(self.buf)
        stride = 0x18
        count = (end - off)//stride
        out: List[dict] = []
        for i in range(count):
            rec = self._slice(off + i*stride, stride)
            i0,i1,i2,i3,flags = struct.unpack_from('<hhhhH', rec, 0)
            # stream indices
            s0,s1,s2,s3 = struct.unpack_from('<hhhh', rec, 0x0E)
            out.append({
                'i': (i0,i1,i2,i3),
                'flags': flags,
                'streams': (s0,s1,s2,s3),
            })
        return out


def _find_psc3_offsets(buf: bytes) -> List[int]:
    magic_bytes = b'PSC3'
    offs: List[int] = []
    start = 0
    while True:
        i = buf.find(magic_bytes, start)
        if i == -1:
            break
        # Verify as little-endian dword equals 0x33435350
        if i + 4 <= len(buf) and struct.unpack_from('<I', buf, i)[0] == PSC3_MAGIC:
            offs.append(i)
        start = i + 1
    return offs


def export_psc3(path: str, dst_dir: str, verbose: bool=False) -> str | None:
    try:
        data = open(path,'rb').read()
    except Exception as e:
        if verbose:
            print(f"[skip] {path}: read error: {e}")
        return None
    # Try whole-file first, then embedded at PSC3 magic locations
    offsets = [0] if (len(data) >= 4 and struct.unpack_from('<I', data, 0)[0] == PSC3_MAGIC) else []
    offsets += _find_psc3_offsets(data)
    tried = set()
    for off in offsets:
        if off in tried:
            continue
        tried.add(off)
        try:
            p = PSC3(data, off)
            verts_raw = p.parse_vertex_records()
            if not verts_raw:
                if verbose:
                    print(f"[no-verts] {path} @0x{off:X}")
                continue
            float4s = p.parse_float4_table()
            colors = p.parse_color_table()
            draw_descs = p.parse_draw_descs()
            if verbose:
                print(f"[ok] {path} @0x{off:X}: verts={len(verts_raw)} float4={len(float4s)} colors={len(colors)} draw={len(draw_descs)}")
            os.makedirs(dst_dir, exist_ok=True)
            base = os.path.splitext(os.path.basename(path))[0]
            suffix = f"_ofs{off}"
            out_path = os.path.join(dst_dir, base + suffix + '.obj')
            with open(out_path,'w',encoding='utf-8') as f:
                f.write(f'# PSC3 geometry from {os.path.basename(path)} @0x{off:X}\n')
                # Write one normal per vertex using per-vertex float4 index if available
                for i,(x,y,z,idx4,extra) in enumerate(verts_raw):
                    f.write(f'v {x:.6f} {y:.6f} {z:.6f}\n')
                    if 0 <= idx4 < len(float4s):
                        nx,ny,nz,_ = float4s[idx4]
                        f.write(f'vn {nx:.6f} {ny:.6f} {nz:.6f}\n')
                    if i < len(colors):
                        r,g,b = colors[i]
                        f.write(f'# vc {r} {g} {b}\n')
                # Faces from draw descriptors
                def safe_idx(idx: int) -> int | None:
                    if 0 <= idx < len(verts_raw):
                        return idx + 1  # OBJ is 1-based
                    return None
                face_written = 0
                for di,dd in enumerate(draw_descs):
                    i0,i1,i2,i3 = dd['i']
                    # Guard against out-of-range indices
                    a = safe_idx(i0); b = safe_idx(i1); c = safe_idx(i2); d = safe_idx(i3)
                    if a is None or b is None or c is None:
                        if verbose:
                            print(f"[warn] skip draw#{di}: OOB index {(i0,i1,i2,i3)}")
                        continue
                    # If i2 == i3 treat as triangle
                    if i2 == i3 or d is None:
                        f.write(f'f {a}//{a} {b}//{b} {c}//{c}\n')
                        face_written += 1
                    else:
                        # Write a quad face. Many viewers accept quads; triangulation can be tuned later.
                        f.write(f'f {a}//{a} {b}//{b} {c}//{c} {d}//{d}\n')
                        face_written += 2  # approximate for count
            if verbose:
                print(f"[wrote] {out_path}")
            return out_path
        except Exception:
            if verbose:
                print(f"[error] {path} @0x{off:X}")
                traceback.print_exc()
            continue
    if verbose:
        print(f"[no-psc3] {path}")
    return None


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description='Export PSC3 geometry to OBJ')
    ap.add_argument('--src', default=SRC_DIR)
    ap.add_argument('--dst', default=DST_DIR)
    ap.add_argument('--limit', type=int, default=100)
    ap.add_argument('--verbose', action='store_true')
    args = ap.parse_args(argv)
    rx = re.compile(r'^map_(\d{4})\.dec\.bin$', re.I)
    files = [os.path.join(args.src, n) for n in sorted(os.listdir(args.src)) if rx.match(n)]
    wrote = 0
    for i,pth in enumerate(files):
        if i >= args.limit:
            break
        out = export_psc3(pth, args.dst, verbose=args.verbose)
        if out:
            wrote += 1
    print(f'Processed {min(args.limit,len(files))} files, wrote {wrote} OBJs to {args.dst}')
    return 0

if __name__ == '__main__':
    import sys
    raise SystemExit(main(sys.argv[1:]))
