#!/usr/bin/env python3
"""PSC3 full extractor — per-submesh grouping, UVs, subdraw colors.

Extends `psc3.py` with knowledge reverse-engineered from:
  - src/FUN_00212058.c   (per-primitive render loop)
  - src/FUN_002129b8.c   (per-vertex stream builder)

Semantics beyond the baseline (see psc3.py for header layout):

  Submesh descriptor (stride 0x14, 10 × s16):
      +0x00  vstream_start     (per-submesh upload window start; not a vertex range)
      +0x02  vstream_end
      +0x04  prim_start        (primitive range, monotonic)
      +0x06  prim_end          (renderer uses max(prim_end) as loop bound)
      +0x08  byte_len          (size of this submesh's slab in Section A)
      +0x0a  material_group    (grouping hint; reused across submeshes)
      +0x0c  aux_id            (unused by renderer — maybe bone-id/skeleton)
      +0x0e  0
      +0x10  u32 byte-offset into Section A (+0x10)
      +0x14  0

  "Materials" table (at offs_materials, stride 10) is actually a
  **per-subdraw UV record**:
      +0x00 .. +0x06  : 4 × u16  UVs, one per primitive corner.
                         Each u16 packs (U << 8) | V as 8-bit texel values,
                         so each corner carries its own (U, V).
                         Triangles leave slot[3] == 0, quads fill all 4.
      +0x08           : u16 tex_flags
        - bit 15 set -> "subdraw invalid" (no draw)
        - bits 14..8  texture atlas slot
        - bits 7..0   texture format/palette

  Colors table is indexed per-subdraw per-corner:
      colors_base + (subdraw_idx * 4 + corner) * 3  -> RGB byte triplet

  Primitive (stride 0x18):
      +0x00 .. +0x06  : v0..v3 (u16)   — v2 == v3 indicates triangle
      +0x08           : u16 flags       (& 0x20 = skip prim; & 8 = per-vertex normal)
      +0x0c           : u8  primary_subdraw_ord  (0..3, offset into the 4 subdraws below)
      +0x0d           : u8  alpha_byte
      +0x0e .. +0x14  : 4 × s16 subdraw-material indices  (-1 = inactive)

  Section A (header +0x10, ~97 KB typical) and Section B (header +0x2C,
  ~56 KB) are NOT touched by the rasterizer — they are consumed by a
  separate path, probably skeletal / bind-pose data used at load time.
  Currently unparsed; submeshes carry a byte_offset into Section A.

Output: one OBJ + MTL pair per PSC3 record. Each submesh becomes a named
group (`sm_NN_mgXX`). Vertex UVs come from the primary subdraw's UV record
(divided by 4096 to roughly normalize PS2-fixed UVs).  Colors become the
Kd channel of the generated material.
"""
from __future__ import annotations

import os
import struct
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

MAGIC_PSC3 = 0x33435350
POS_SCALE = 1.0 / 2048.0
UV_SCALE = 1.0 / 255.0  # UVs are packed 8+8 in a single u16 (U<<8 | V), byte range
# Rest-pose scale constants (from FUN_0020da68, PSC3 animation sampler):
#   quat.xyz = s16 * 0.00048828125 = s16 / 2048
#   quat.w   = s16 * 0.00024414063 = s16 / 4096
# The first quat-xyz value set to 0x7fff is a sentinel meaning "no
# valid transform for this submesh" — the sampler falls back to a
# zeroed vector and w=1 identity. We honor the same sentinel here.
QUAT_XYZ_SCALE = 1.0 / 2048.0
QUAT_W_SCALE = 1.0 / 4096.0
QUAT_SENTINEL = 0x7fff
# Translations: s16 / fGpffff80fc. The exact gp-relative constant is
# unknown but empirically plausible values are 1024/2048/4096. We
# default to 2048 to match the typical PS2 fixed-point coordinate scale.
TRANS_SCALE = 1.0 / 2048.0


def _u16(b, o): return struct.unpack_from('<H', b, o)[0]
def _s16(b, o): return struct.unpack_from('<h', b, o)[0]
def _u32(b, o): return struct.unpack_from('<I', b, o)[0]
def _f32(b, o): return struct.unpack_from('<f', b, o)[0]


@dataclass
class Submesh:
    index: int
    vstream_start: int
    vstream_end: int
    prim_start: int
    prim_end: int
    byte_len: int
    material_group: int
    aux_id: int
    section_a_off: int  # absolute file offset
    # Bone hierarchy: low byte of `material_group` is the parent submesh
    # index. A self-loop (parent == self) marks the world root. -1 means
    # no parent (root or unparented).
    parent_idx: int = -1
    # Rest-pose transform resolved via slab[0] -> Section B. None when absent.
    rest_quat: Optional[Tuple[float, float, float, float]] = None  # (x, y, z, w)
    rest_trans: Optional[Tuple[float, float, float]] = None


@dataclass
class Subdraw:
    index: int
    uvs: Tuple[int, int, int, int]
    tex_flags: int


@dataclass
class Primitive:
    index: int
    v: Tuple[int, int, int, int]
    flags: int
    primary_subdraw: int
    alpha: int
    subdraws: Tuple[int, int, int, int]

    @property
    def is_triangle(self) -> bool:
        return self.v[2] == self.v[3]

    def active_subdraws(self) -> List[int]:
        return [s for s in self.subdraws if s != -1]


@dataclass
class PSC3FullMesh:
    header: dict = field(default_factory=dict)
    positions: List[Tuple[float, float, float]] = field(default_factory=list)
    vertex_bytes: List[int] = field(default_factory=list)
    vertex_normal_idx: List[int] = field(default_factory=list)
    normal_table: List[Tuple[float, float, float]] = field(default_factory=list)
    primitives: List[Primitive] = field(default_factory=list)
    submeshes: List[Submesh] = field(default_factory=list)
    subdraws: List[Subdraw] = field(default_factory=list)
    colors: bytes = b""   # raw 3-byte RGB stream

    def primitive_submesh(self, prim_index: int) -> Optional[Submesh]:
        for sm in self.submeshes:
            if sm.prim_start <= prim_index < sm.prim_end:
                return sm
        return None

    def color_for(self, subdraw_idx: int, corner: int) -> Tuple[int, int, int]:
        # Colors are a flat RGB byte table. Per FUN_002129b8, the per-corner
        # stride is 3 bytes (one RGB triplet), with the base offset being
        # ``material_index * 3``. So corner N of a primitive whose subdraw
        # has material index M reads ``colors[(M + N) * 3]``. This means
        # adjacent material indices share color entries when primitives
        # overlap in color space -- not a per-subdraw "group of 4" layout.
        base = (subdraw_idx + corner) * 3
        if 0 <= base and base + 3 <= len(self.colors):
            return (self.colors[base], self.colors[base + 1], self.colors[base + 2])
        return (128, 128, 128)


def parse_psc3_full(buf: bytes, pose_frame: int = 0) -> PSC3FullMesh:
    """Parse a PSC3 chunk.

    ``pose_frame`` selects which entry of each submesh's pose table is
    baked into ``Submesh.rest_quat`` / ``Submesh.rest_trans``. Frame 0
    is the default and matches the historical "rest pose" behavior.
    Higher values let callers scrub through the animation without
    touching the sampler directly. Out-of-range requests fall back to
    identity (sampler-equivalent behavior).
    """
    if len(buf) < 0x44 or _u32(buf, 0) != MAGIC_PSC3:
        raise ValueError("not a PSC3 chunk")

    h = {
        'magic':             _u32(buf, 0x00),
        'submesh_count':     _s16(buf, 0x04),
        'offs_submeshes':    _u32(buf, 0x08),
        'offs_u0c':          _u32(buf, 0x0C),  # 0x28-byte record table (unparsed)
        'offs_section_a':    _u32(buf, 0x10),  # ~97KB aux (unparsed)
        'offs_vertices':     _u32(buf, 0x14),
        'offs_vertex_bytes': _u32(buf, 0x18),
        'offs_primitives':   _u32(buf, 0x1C),
        'offs_colors':       _u32(buf, 0x20),
        'offs_materials':    _u32(buf, 0x24),  # actually UV records
        'offs_normals':      _u32(buf, 0x28),
        'offs_section_b':    _u32(buf, 0x2C),  # ~56KB aux (unparsed)
        'file_size':         len(buf),
    }
    mesh = PSC3FullMesh(header=h)

    # ---------- submeshes ----------
    for i in range(max(0, h['submesh_count'])):
        base = h['offs_submeshes'] + i * 0x14
        if base + 0x14 > len(buf):
            break
        vstart = _s16(buf, base + 0x00)
        vend = _s16(buf, base + 0x02)
        pstart = _s16(buf, base + 0x04)
        pend = _s16(buf, base + 0x06)
        byte_len = _s16(buf, base + 0x08)
        mgrp = _s16(buf, base + 0x0A)
        aux = _s16(buf, base + 0x0C)
        # field[8..9] is a u32 pose-table offset — ABSOLUTE within the PSC3
        # (psc3_base + sec_a_off), NOT relative to offs_section_a. Verified
        # by re-reading FUN_0020da68 against small meshes like grp_017c.
        sec_a_off = _u32(buf, base + 0x10)
        # Parent index lives in the low byte of `material_group`. The high
        # byte appears to encode flags (skinning/weighting hints). Self-loop
        # marks the world root (typically the last submesh, with
        # byte_len == 0 -- a joint-only node).
        parent = mgrp & 0xff
        if parent == i or parent >= max(1, h['submesh_count']):
            parent = -1
        mesh.submeshes.append(Submesh(
            index=i, vstream_start=vstart, vstream_end=vend,
            prim_start=pstart, prim_end=pend, byte_len=byte_len,
            material_group=mgrp, aux_id=aux,
            section_a_off=sec_a_off, parent_idx=parent,
        ))

    # ---------- rest-pose resolution (pose-table slab[0] -> keyframe pool) ----------
    # Each submesh's pose-table slab is a u32 array at psc3+sec_a_off.
    # slab[i] packs (rot_idx << 0) | (trs_idx << 16); indices are u16
    # strides into the keyframe pool rooted at psc3+offs_section_b.
    # 0xFFFF -> identity rot / zero trans. If the 4-short quat starts
    # with 0x7fff, the sampler treats the entire pose as zero/identity
    # (FUN_0020da68 LAB_0020dbfc path).
    secB = h['offs_section_b']
    if secB:
        for sm in mesh.submeshes:
            if sm.section_a_off == 0 or sm.byte_len <= 0:
                continue
            n_poses = sm.byte_len // 4
            frame = pose_frame if 0 <= pose_frame < n_poses else 0
            slab_entry = sm.section_a_off + frame * 4
            if slab_entry + 4 > len(buf):
                continue
            u32 = _u32(buf, slab_entry)
            quat_idx = u32 & 0xFFFF
            trans_idx = (u32 >> 16) & 0xFFFF
            if quat_idx != 0xFFFF:
                qbase = secB + quat_idx * 2
                if qbase + 8 <= len(buf):
                    q = struct.unpack_from('<4h', buf, qbase)
                    # Honor the 0x7fff "no valid transform" sentinel used by
                    # the in-game sampler (FUN_0020da68 LAB_0020dbfc path).
                    if q[0] != QUAT_SENTINEL:
                        sm.rest_quat = (
                            q[0] * QUAT_XYZ_SCALE, q[1] * QUAT_XYZ_SCALE,
                            q[2] * QUAT_XYZ_SCALE, q[3] * QUAT_W_SCALE,
                        )
            if trans_idx != 0xFFFF:
                tbase = secB + trans_idx * 2
                if tbase + 6 <= len(buf):
                    t = struct.unpack_from('<3h', buf, tbase)
                    sm.rest_trans = (
                        t[0] * TRANS_SCALE, t[1] * TRANS_SCALE,
                        t[2] * TRANS_SCALE,
                    )

    # ---------- section sizing helper ----------
    # Build a sorted list of section offsets to bound each array.
    all_sections = sorted({o for o in (
        h['offs_submeshes'], h['offs_u0c'], h['offs_section_a'],
        h['offs_vertices'], h['offs_vertex_bytes'], h['offs_primitives'],
        h['offs_colors'], h['offs_materials'], h['offs_normals'],
        h['offs_section_b'],
    ) if o}) + [len(buf)]

    def _end_of(off: int) -> int:
        for o in all_sections:
            if o > off:
                return o
        return len(buf)

    # ---------- vertices ----------
    if h['offs_vertices']:
        end = _end_of(h['offs_vertices'])
        n = (end - h['offs_vertices']) // 10
        for i in range(n):
            p = h['offs_vertices'] + i * 10
            x = _s16(buf, p) * POS_SCALE
            y = _s16(buf, p + 2) * POS_SCALE
            z = _s16(buf, p + 4) * POS_SCALE
            ni = _u16(buf, p + 6)
            mesh.positions.append((x, y, z))
            mesh.vertex_normal_idx.append(ni)

    # ---------- vertex bytes (per-vertex alpha/intensity) ----------
    if h['offs_vertex_bytes']:
        end = _end_of(h['offs_vertex_bytes'])
        n = end - h['offs_vertex_bytes']
        mesh.vertex_bytes = list(buf[h['offs_vertex_bytes']:h['offs_vertex_bytes'] + n])

    # ---------- normals ----------
    if h['offs_normals']:
        end = _end_of(h['offs_normals'])
        n = (end - h['offs_normals']) // 0x10
        for i in range(n):
            p = h['offs_normals'] + i * 0x10
            if p + 12 > len(buf):
                break
            mesh.normal_table.append((_f32(buf, p), _f32(buf, p + 4), _f32(buf, p + 8)))

    # ---------- subdraw (UV) records ----------
    if h['offs_materials']:
        end = _end_of(h['offs_materials'])
        n = (end - h['offs_materials']) // 10
        for i in range(n):
            p = h['offs_materials'] + i * 10
            u0, u1, u2, u3, flags = struct.unpack_from('<5H', buf, p)
            mesh.subdraws.append(Subdraw(
                index=i, uvs=(u0, u1, u2, u3), tex_flags=flags,
            ))

    # ---------- colors ----------
    if h['offs_colors']:
        end = _end_of(h['offs_colors'])
        mesh.colors = bytes(buf[h['offs_colors']:end])

    # ---------- primitives ----------
    if h['offs_primitives']:
        # Outer bound = max prim_end across submeshes; fall back to section size.
        prim_count = 0
        if mesh.submeshes:
            prim_count = max(sm.prim_end for sm in mesh.submeshes)
        if prim_count <= 0:
            end = _end_of(h['offs_primitives'])
            prim_count = (end - h['offs_primitives']) // 0x18
        for i in range(prim_count):
            base = h['offs_primitives'] + i * 0x18
            if base + 0x18 > len(buf):
                break
            v0, v1, v2, v3 = struct.unpack_from('<4H', buf, base + 0x00)
            flags = _u16(buf, base + 0x08)
            primary = buf[base + 0x0C]
            alpha = buf[base + 0x0D]
            sd = struct.unpack_from('<4h', buf, base + 0x0E)
            mesh.primitives.append(Primitive(
                index=i, v=(v0, v1, v2, v3), flags=flags,
                primary_subdraw=primary, alpha=alpha, subdraws=sd,
            ))

    return mesh


# ---------------------------------------------------------------------------
# Animation sampler — Python port of FUN_0020da68
# ---------------------------------------------------------------------------


def sample_pose(buf: bytes, mesh: PSC3FullMesh, bone_id: int, pose_idx: int
                ) -> Tuple[Tuple[float, float, float], Tuple[float, float, float, float]]:
    """Resolve a single (translation, quaternion) for one bone's pose slot.

    Mirrors FUN_0020da68 logic exactly (modulo the external anim-table
    indirection — here `pose_idx` is the direct index into the submesh's
    own pose table). Returns identity (0,0,0), (0,0,0,1) for invalid or
    out-of-range inputs, matching the game's fallback.
    """
    ident = ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0))
    if bone_id < 0 or bone_id >= len(mesh.submeshes):
        return ident
    sm = mesh.submeshes[bone_id]
    if sm.section_a_off == 0 or sm.byte_len <= 0:
        return ident
    n_poses = sm.byte_len // 4
    if pose_idx < 0 or pose_idx >= n_poses:
        return ident
    slab_entry = sm.section_a_off + pose_idx * 4
    if slab_entry + 4 > len(buf):
        return ident
    packed = _u32(buf, slab_entry)
    rot_idx = packed & 0xFFFF
    trs_idx = (packed >> 16) & 0xFFFF
    secB = mesh.header['offs_section_b']

    # Rotation
    qx = qy = qz = 0.0
    qw = 1.0
    sentinel_hit = False
    if rot_idx != 0xFFFF and secB:
        qbase = secB + rot_idx * 2
        if qbase + 8 <= len(buf):
            q = struct.unpack_from('<4h', buf, qbase)
            if q[0] == QUAT_SENTINEL:
                # Sampler falls back to zero+identity for BOTH rot and trans.
                sentinel_hit = True
            else:
                qx = q[0] * QUAT_XYZ_SCALE
                qy = q[1] * QUAT_XYZ_SCALE
                qz = q[2] * QUAT_XYZ_SCALE
                qw = q[3] * QUAT_W_SCALE

    # Translation
    tx = ty = tz = 0.0
    if not sentinel_hit and trs_idx != 0xFFFF and secB:
        tbase = secB + trs_idx * 2
        if tbase + 6 <= len(buf):
            t = struct.unpack_from('<3h', buf, tbase)
            tx = t[0] * TRANS_SCALE
            ty = t[1] * TRANS_SCALE
            tz = t[2] * TRANS_SCALE

    return ((tx, ty, tz), (qx, qy, qz, qw))


# ---------------------------------------------------------------------------
# Corrected pose sampler (2026-04 reverse-engineering pass).
#
# Re-tracing the consumer chain (FUN_0020d188 -> FUN_0020d618 ->
# FUN_0020cf28) revealed that the 7-float buffer FUN_0020da68 fills is
# NOT (translation, quaternion). Instead the matrix builder treats the
# 7 floats as 4 separate channels, with these indices:
#
#   idx 3       -> uniform SCALE  (passed twice: scale_x = scale_y/z)
#   idx 4,5,6   -> EULER X, Y, Z  (interpolated as ANGLES, then negated
#                                  before being applied; binary-angle
#                                  encoding -> radians)
#   idx 0,1,2   -> TRANSLATION X, Y, Z
#
# Mapped back through FUN_0020d8c0's repacking, the pool layout in the
# PSC3 keyframe blob is therefore:
#
#   "rot" pool (4 shorts, indexed by slab[i] & 0xffff):
#       (trans_x, trans_y, trans_z, scale)
#       trans_xyz scaled by 1/2048; scale by 1/4096.
#       First short == 0x7fff is the "identity" sentinel.
#   "trans" pool (3 shorts, indexed by slab[i] >> 16):
#       (euler_x, euler_y, euler_z) in binary-angle units;
#       runtime divides by fGpffff80fc to get radians.
#       0xffff index -> identity (zero).
#
# The natural binary-angle convention on PS2 maps int16 -32768..+32767
# to (-pi..+pi) -> divisor = 32768/pi ~= 10430.378. We default to that.
# ---------------------------------------------------------------------------
import math as _math
EULER_DIVISOR = 32768.0 / _math.pi
SCALE_DENOM = 4096.0


def sample_pose_v2(buf: bytes, mesh: PSC3FullMesh, sm_id: int, pose_idx: int):
    """Return ((tx, ty, tz), (ex, ey, ez), scale) for one submesh/pose.

    Translation in mesh units (1/2048 fixed-point), euler in radians,
    scale as a unit-less float (1.0 == identity).
    """
    ident = ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0), 1.0)
    if sm_id < 0 or sm_id >= len(mesh.submeshes):
        return ident
    sm = mesh.submeshes[sm_id]
    if sm.section_a_off == 0 or sm.byte_len <= 0:
        return ident
    n_poses = sm.byte_len // 4
    # When an animation references a pose index higher than this
    # submesh has data for, the runtime leaves the bone at its bind/
    # rest pose (target 0) rather than snapping to identity. Returning
    # identity would collapse low-pose-count bones like fingers
    # (8-9 poses) to their parent's origin during animations whose
    # targets reach into the high tens or low hundreds.
    if pose_idx < 0:
        return ident
    if pose_idx >= n_poses:
        if n_poses <= 0:
            return ident
        pose_idx = 0
    slab_entry = sm.section_a_off + pose_idx * 4
    if slab_entry + 4 > len(buf):
        return ident
    packed = _u32(buf, slab_entry)
    rot_idx = packed & 0xFFFF       # -> trans+scale pool
    trans_idx = (packed >> 16) & 0xFFFF  # -> euler pool
    secB = mesh.header['offs_section_b']

    tx = ty = tz = 0.0
    scale = 1.0
    sentinel_hit = False
    if rot_idx != 0xFFFF and secB:
        base = secB + rot_idx * 2
        if base + 8 <= len(buf):
            s = struct.unpack_from('<4h', buf, base)
            if s[0] == QUAT_SENTINEL:
                sentinel_hit = True
            else:
                tx = s[0] / 2048.0
                ty = s[1] / 2048.0
                tz = s[2] / 2048.0
                scale = s[3] / SCALE_DENOM

    ex = ey = ez = 0.0
    if not sentinel_hit and trans_idx != 0xFFFF and secB:
        base = secB + trans_idx * 2
        if base + 6 <= len(buf):
            s = struct.unpack_from('<3h', buf, base)
            ex = s[0] / EULER_DIVISOR
            ey = s[1] / EULER_DIVISOR
            ez = s[2] / EULER_DIVISOR

    return ((tx, ty, tz), (ex, ey, ez), scale)


def max_pose_count(mesh: PSC3FullMesh) -> int:
    """Return the largest pose-table length across all submeshes."""
    best = 0
    for sm in mesh.submeshes:
        if sm.byte_len > 0:
            best = max(best, sm.byte_len // 4)
    return best


def _norm_for(mesh: PSC3FullMesh, vi: int) -> Tuple[float, float, float]:
    if 0 <= vi < len(mesh.vertex_normal_idx):
        ni = mesh.vertex_normal_idx[vi]
        if 0 <= ni < len(mesh.normal_table):
            return mesh.normal_table[ni]
    return (0.0, 0.0, 1.0)


def _quat_rotate(q: Tuple[float, float, float, float],
                 v: Tuple[float, float, float]) -> Tuple[float, float, float]:
    """Rotate v by quaternion q = (x, y, z, w).

    Standard Rodrigues formula; q need not be unit-length.
    """
    x, y, z, w = q
    vx, vy, vz = v
    # t = 2 * cross(q.xyz, v)
    tx = 2.0 * (y * vz - z * vy)
    ty = 2.0 * (z * vx - x * vz)
    tz = 2.0 * (x * vy - y * vx)
    # v + w*t + cross(q.xyz, t)
    rx = vx + w * tx + (y * tz - z * ty)
    ry = vy + w * ty + (z * tx - x * tz)
    rz = vz + w * tz + (x * ty - y * tx)
    return (rx, ry, rz)


def _pose_vertex(sm: Submesh,
                 p: Tuple[float, float, float]) -> Tuple[float, float, float]:
    """Apply a submesh's rest-pose transform (quat then translation) to a point."""
    v = p
    if sm.rest_quat is not None:
        v = _quat_rotate(sm.rest_quat, v)
    if sm.rest_trans is not None:
        v = (v[0] + sm.rest_trans[0],
             v[1] + sm.rest_trans[1],
             v[2] + sm.rest_trans[2])
    return v


def _pose_normal(sm: Submesh,
                 n: Tuple[float, float, float]) -> Tuple[float, float, float]:
    """Rotate a normal by the submesh rest-pose quaternion (no translation)."""
    if sm.rest_quat is None:
        return n
    return _quat_rotate(sm.rest_quat, n)


def write_obj_mtl(mesh: PSC3FullMesh, obj_path: str, name: str = "mesh",
                  apply_rest_pose: bool = False,
                  bundle_dir: Optional[str] = None) -> dict:
    """Emit OBJ (with per-submesh groups + UVs) and matching MTL.

    When ``apply_rest_pose`` is set, each submesh's rest-pose transform
    (quaternion + translation resolved via Section B) is applied to the
    vertices. Because a single vertex may be referenced by primitives
    belonging to different submeshes, each (original_vertex, submesh)
    pair is emitted as its own unique OBJ vertex so submeshes don't
    smear into each other via shared verts.

    Returns statistics dict.
    """
    mtl_path = os.path.splitext(obj_path)[0] + ".mtl"
    mtl_rel = os.path.basename(mtl_path)

    n_orig_verts = len(mesh.positions)
    stats = {
        'verts': n_orig_verts, 'normals': len(mesh.normal_table),
        'prims_total': len(mesh.primitives), 'prims_skip_flag': 0,
        'prims_no_subdraw': 0, 'tris': 0, 'quads': 0,
        'submeshes': len(mesh.submeshes), 'materials': 0,
    }
    # Per-(orig_vi, sm_idx) deduplicated vertex table. This avoids tris
    # stretching between different submeshes' transforms.
    vert_table: List[Tuple[int, int]] = []          # new_vi -> (orig_vi, sm_idx)
    vert_map: dict = {}                             # (orig_vi, sm_idx) -> new_vi

    def _remap(orig_vi: int, sm_idx: int) -> int:
        key = (orig_vi, sm_idx)
        new_vi = vert_map.get(key)
        if new_vi is None:
            new_vi = len(vert_table)
            vert_table.append(key)
            vert_map[key] = new_vi
        return new_vi
    # Pre-compute UV list and VT list (OBJ face refs use 1-based vt indices).
    # We emit one vt entry per primitive-corner-use that's actually referenced
    # so the file stays compact.
    vt_entries: List[Tuple[float, float]] = []

    # Pre-compute a unique material per subdraw index that's actually drawn.
    # Subdraws sharing the same RGB + tex_flags collapse into one material.
    mat_keys: dict = {}           # (r,g,b,flags) -> mat_name
    mat_order: List[str] = []
    mat_data: dict = {}           # mat_name -> dict(Kd, flags)

    def _get_material(subdraw_idx: int, corner: int) -> str:
        sd = mesh.subdraws[subdraw_idx] if 0 <= subdraw_idx < len(mesh.subdraws) else None
        flags = sd.tex_flags if sd else 0
        r, g, b = mesh.color_for(subdraw_idx, corner)
        key = (r, g, b, flags)
        if key in mat_keys:
            return mat_keys[key]
        name = f"mat_{len(mat_order):04d}"
        mat_keys[key] = name
        mat_order.append(name)
        mat_data[name] = {
            'Kd': (r / 255.0, g / 255.0, b / 255.0),
            'flags': flags,
            'sample_subdraw': subdraw_idx,
        }
        return name

    # Build primitive -> (submesh, active_subdraw_idx) resolution.
    # Primary subdraw is byte +0xC (0..3 ordinal).
    def _primary_subdraw_mat_idx(p: Primitive) -> Optional[int]:
        if p.primary_subdraw < 4:
            sd = p.subdraws[p.primary_subdraw]
            if sd != -1:
                return sd
        # Fallback: first non -1.
        for sd in p.subdraws:
            if sd != -1:
                return sd
        return None

    # Pass 1: decide faces and allocate vt entries / materials.
    # Each primitive produces 1 tri or 2 tris; we store face descriptors.
    # Face format: list of (submesh, material_name, [(v_idx, vt_idx, vn_idx)*3])
    faces: List[Tuple[Submesh, str, List[Tuple[int, int, int]]]] = []

    for p in mesh.primitives:
        if p.flags & 0x20:
            stats['prims_skip_flag'] += 1
            continue
        sd_mat = _primary_subdraw_mat_idx(p)
        if sd_mat is None:
            stats['prims_no_subdraw'] += 1
            continue
        sm = mesh.primitive_submesh(p.index)
        if sm is None:
            continue

        sd = mesh.subdraws[sd_mat] if 0 <= sd_mat < len(mesh.subdraws) else None
        # UV encoding: each u16 slot is one corner's (U,V) packed as
        # (U << 8) | V (bytes). Confirmed experimentally: triangles have
        # slot[3] == 0, quads fill all 4 slots, matching the primitive
        # vertex count exactly.
        corner_uvs: List[Tuple[float, float]] = []
        if sd is not None:
            for slot in sd.uvs:
                # UV byte order: low byte = U, high byte = V.
                # The PS2 PSC3 V axis already matches the OBJ/glTF
                # convention (origin top-left, V increases downward in
                # texel space -> stored as-is), so no flip is needed.
                u = (slot & 0xFF) * UV_SCALE
                v = ((slot >> 8) & 0xFF) * UV_SCALE
                corner_uvs.append((u, v))
        else:
            corner_uvs = [(0.0, 0.0)] * 4

        # Allocate vt entries for the corners we'll actually use.
        def _alloc_vt(uv: Tuple[float, float]) -> int:
            vt_entries.append(uv)
            return len(vt_entries)  # 1-based

        if p.is_triangle:
            # Tri: corners (0,1,2), verts (v0,v1,v2)
            rv0 = _remap(p.v[0], sm.index)
            rv1 = _remap(p.v[1], sm.index)
            rv2 = _remap(p.v[2], sm.index)
            c0 = (rv0, _alloc_vt(corner_uvs[0]), rv0 + 1)
            c1 = (rv1, _alloc_vt(corner_uvs[1]), rv1 + 1)
            c2 = (rv2, _alloc_vt(corner_uvs[2]), rv2 + 1)
            # Per-corner material pick uses corner 0 color.
            mat_name = _get_material(sd_mat, 0)
            faces.append((sm, mat_name, [c0, c1, c2]))
            stats['tris'] += 1
        else:
            # Quad: emitted as two tris (v3,v0,v1) & (v1,v2,v3)
            rv0 = _remap(p.v[0], sm.index)
            rv1 = _remap(p.v[1], sm.index)
            rv2 = _remap(p.v[2], sm.index)
            rv3 = _remap(p.v[3], sm.index)
            uv0 = _alloc_vt(corner_uvs[0])
            uv1 = _alloc_vt(corner_uvs[1])
            uv2 = _alloc_vt(corner_uvs[2])
            uv3 = _alloc_vt(corner_uvs[3])
            mat_name = _get_material(sd_mat, 0)
            faces.append((sm, mat_name, [
                (rv3, uv3, rv3 + 1),
                (rv0, uv0, rv0 + 1),
                (rv1, uv1, rv1 + 1),
            ]))
            faces.append((sm, mat_name, [
                (rv1, uv1, rv1 + 1),
                (rv2, uv2, rv2 + 1),
                (rv3, uv3, rv3 + 1),
            ]))
            stats['quads'] += 1

    stats['materials'] = len(mat_order)

    # ---------------- write MTL ----------------
    # Gather candidate BMPA PNGs from the bundle directory so artists can
    # cycle through them in Blender for visual probing. The PS2 renderer
    # binds BMPA textures via the entity/scene descriptor (external to
    # PSC3) so we cannot derive the exact mapping from PSC3 alone; we
    # emit a best-effort active guess plus commented-out alternates.
    bmpa_pngs: List[str] = []
    preferred_png: Optional[str] = None
    if bundle_dir and os.path.isdir(bundle_dir):
        try:
            for fn in sorted(os.listdir(bundle_dir)):
                if fn.startswith('tex_') and fn.endswith('.png'):
                    bmpa_pngs.append(fn)
            # Heuristic: if this PSC3's basename shares its 4-hex rid with
            # a BMPA in the same bundle, prefer that one (observed in
            # bundle s00_e000 where grp_0172 and tex_0172 co-exist).
            if name.startswith('grp_'):
                rid = name[len('grp_'):]
                match = f"tex_{rid}.png"
                if match in bmpa_pngs:
                    preferred_png = match
            if preferred_png is None and bmpa_pngs:
                preferred_png = bmpa_pngs[0]
        except OSError:
            pass

    with open(mtl_path, 'w', encoding='utf-8') as fm:
        fm.write(f"# Auto-generated from {name}\n")
        if bmpa_pngs:
            fm.write(f"# bundle has {len(bmpa_pngs)} BMPA textures: {', '.join(bmpa_pngs)}\n")
            if preferred_png:
                fm.write(f"# active map_Kd guess: {preferred_png} "
                         f"(edit the `map_Kd` line per-material to probe others)\n")
        for mname in mat_order:
            d = mat_data[mname]
            kd = d['Kd']
            fm.write(f"newmtl {mname}\n")
            fm.write(f"Kd {kd[0]:.4f} {kd[1]:.4f} {kd[2]:.4f}\n")
            fm.write("Ka 0.0 0.0 0.0\n")
            fm.write("Ks 0.0 0.0 0.0\n")
            fm.write("d 1.0\nillum 1\n")
            flags = d['flags']
            slot = flags & 0x7f          # GS texture slot / TBP selector
            palette = (flags >> 7) & 0xf  # CLUT/palette selector
            fmt = (flags >> 11) & 0x7    # texture format bits
            enable = (flags >> 14) & 0x3  # nonzero => textured
            no_tex = (slot == 0x7f) or (enable == 0)
            if flags:
                fm.write(f"# tex_flags 0x{flags:04x} "
                         f"slot={slot} palette={palette} fmt={fmt} "
                         f"enable={enable}{' (untextured)' if no_tex else ''}\n")
            # Emit map_Kd candidates only for materials that actually want a
            # texture. Vertex-colored (untextured) mats stay plain.
            if not no_tex and bmpa_pngs:
                if preferred_png:
                    fm.write(f"map_Kd {preferred_png}\n")
                fm.write("# alt candidates (uncomment one to override):\n")
                for fn in bmpa_pngs:
                    if fn == preferred_png:
                        continue
                    fm.write(f"# map_Kd {fn}\n")
            fm.write("\n")

    # ---------------- write OBJ ----------------
    n_faces_written = 0
    n_emit = len(vert_table)
    with open(obj_path, 'w', encoding='utf-8') as f:
        f.write(f"# PSC3 mesh: {name}\n")
        f.write(f"# verts={n_orig_verts} emitted={n_emit} "
                f"normals={len(mesh.normal_table)} "
                f"prims={len(mesh.primitives)} submeshes={len(mesh.submeshes)} "
                f"subdraws={len(mesh.subdraws)} mats={len(mat_order)}\n")
        f.write(f"mtllib {mtl_rel}\n")
        f.write(f"o {name}\n")

        # Positions: Z-up -> Y-up (x, y, z) -> (x, z, -y).
        # Each (orig_vi, sm_idx) pair gets its own posed copy.
        for orig_vi, sm_idx in vert_table:
            p = mesh.positions[orig_vi]
            sm = mesh.submeshes[sm_idx]
            if apply_rest_pose:
                p = _pose_vertex(sm, p)
            x, y, z = p
            f.write(f"v {x:.6f} {z:.6f} {-y:.6f}\n")
        for uv in vt_entries:
            f.write(f"vt {uv[0]:.6f} {uv[1]:.6f}\n")
        for orig_vi, sm_idx in vert_table:
            n = _norm_for(mesh, orig_vi)
            if apply_rest_pose:
                sm = mesh.submeshes[sm_idx]
                n = _pose_normal(sm, n)
            nx, ny, nz = n
            f.write(f"vn {nx:.6f} {nz:.6f} {-ny:.6f}\n")

        # Group by submesh, then by material.
        by_submesh: dict = {}
        for sm, mat, corners in faces:
            by_submesh.setdefault(sm.index, []).append((mat, corners))

        for sm_index in sorted(by_submesh.keys()):
            sm = mesh.submeshes[sm_index]
            f.write(f"g sm_{sm_index:02d}_mg{sm.material_group}\n")
            current_mat = None
            # Group faces by material within the submesh for fewer `usemtl` switches.
            by_submesh[sm_index].sort(key=lambda t: t[0])
            for mat, corners in by_submesh[sm_index]:
                if mat != current_mat:
                    f.write(f"usemtl {mat}\n")
                    current_mat = mat
                # Skip face if any vertex index is out of range.
                if not all(0 <= c[0] < n_emit for c in corners):
                    continue
                parts = [f"{c[0]+1}/{c[1]}/{c[2]}" for c in corners]
                f.write(f"f {' '.join(parts)}\n")
                n_faces_written += 1

    stats['faces_written'] = n_faces_written
    stats['mtl_path'] = mtl_path
    return stats


def export_file(src_path: str, dst_dir: str, verbose: bool = False,
                apply_rest_pose: bool = False) -> Optional[dict]:
    data = open(src_path, 'rb').read()
    if len(data) < 4 or _u32(data, 0) != MAGIC_PSC3:
        if verbose:
            print(f"[skip] {src_path}: not PSC3")
        return None
    os.makedirs(dst_dir, exist_ok=True)
    base = os.path.splitext(os.path.basename(src_path))[0]
    try:
        mesh = parse_psc3_full(data)
    except Exception as e:
        if verbose:
            print(f"[err]  {src_path}: {e}")
        return None
    obj_path = os.path.join(dst_dir, base + ".obj")
    # For BMPA neighbor lookup, treat the source file's directory as the bundle.
    bundle_dir = os.path.dirname(os.path.abspath(src_path))
    stats = write_obj_mtl(mesh, obj_path, name=base, apply_rest_pose=apply_rest_pose,
                          bundle_dir=bundle_dir)
    if verbose:
        print(f"[ok]   {obj_path}  "
              f"sm={stats['submeshes']}  faces={stats['faces_written']}  "
              f"mats={stats['materials']}")
    stats['obj_path'] = obj_path
    return stats


def main(argv=None) -> int:
    import argparse
    ap = argparse.ArgumentParser(description="PSC3 full extractor (OBJ+MTL)")
    ap.add_argument('--src', required=True, help="PSC3 file or directory")
    ap.add_argument('--dst', required=True, help="Output directory")
    ap.add_argument('--limit', type=int, default=None)
    ap.add_argument('-v', '--verbose', action='store_true')
    ap.add_argument('--pose', action='store_true',
                    help="Apply experimental rest-pose transforms (Section B slab[0]).")
    args = ap.parse_args(argv)

    inputs: List[str] = []
    if os.path.isdir(args.src):
        for name in sorted(os.listdir(args.src)):
            full = os.path.join(args.src, name)
            if os.path.isfile(full) and name.lower().endswith(('.psc3', '.bin')):
                inputs.append(full)
    else:
        inputs.append(args.src)
    if args.limit is not None:
        inputs = inputs[:args.limit]

    ok = 0
    for p in inputs:
        if export_file(p, args.dst, verbose=args.verbose,
                       apply_rest_pose=args.pose):
            ok += 1
    print(f"Processed {len(inputs)} file(s); {ok} extracted to {args.dst}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
