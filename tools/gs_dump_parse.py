"""PCSX2 GS dump parser.

Reads a .gs.zst (or .gs) PCSX2 GS dump and extracts per-vertex
(ST or UV, XYZ) tuples grouped by draw call. Used to ground-truth
the PSM2 mesh format.

GS dump layout:
    u32  magic = 0xFFFFFFFF
    u32  header_size
    GSDumpHeader (9 u32):
        state_version, state_size, serial_offset, serial_size,
        crc, screenshot_width, screenshot_height,
        screenshot_offset, screenshot_size
    char serial[serial_size]
    u32  screenshot_pixels[screenshot_w * screenshot_h]
    u8   state[state_size]
    u8   regs[sizeof(GSPrivRegSet)]
    packets...

Packets:
    Type byte:
      0 (Transfer): u8 path, u32 size, u8 data[size]
      1 (VSync):    u8 field
      2 (ReadFIFO): u32 size
      3 (Registers): GSPrivRegSet

GIFTAG (16 bytes):
    QW0: NLOOP[14], EOP[1], pad[16], PRE[1], PRIM[11], FLG[2], NREG[4]
    QW1: REGS[64]  (16 4-bit register descriptors)

Packed register types (FLG=0):
    0x0=PRIM 0x1=RGBAQ 0x2=ST 0x3=UV 0x4=XYZF2 0x5=XYZ2
    0x6=TEX0_1 0x7=TEX0_2 0x8=CLAMP_1 0x9=CLAMP_2 0xA=FOG
    0xC=XYZF3 0xD=XYZ3 0xE=A+D 0xF=NOP
"""

from __future__ import annotations
import struct, sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional, Tuple


GS_PRIV_REG_SIZE = 0x2000  # GSPrivRegSet size; standard 8KB priv reg block


# Packed register types
REG_PRIM    = 0x0
REG_RGBAQ   = 0x1
REG_ST      = 0x2
REG_UV      = 0x3
REG_XYZF2   = 0x4
REG_XYZ2    = 0x5
REG_TEX0_1  = 0x6
REG_TEX0_2  = 0x7
REG_FOG     = 0xA
REG_XYZF3   = 0xC
REG_XYZ3    = 0xD
REG_AD      = 0xE
REG_NOP     = 0xF

# A+D destination addresses (subset)
ADDR_PRIM   = 0x00
ADDR_RGBAQ  = 0x01
ADDR_ST     = 0x02
ADDR_UV     = 0x03
ADDR_XYZF2  = 0x04
ADDR_XYZ2   = 0x05
ADDR_TEX0_1 = 0x06
ADDR_TEX0_2 = 0x07
ADDR_PRMODE = 0x1A
ADDR_TEX1_1 = 0x14
ADDR_TEX1_2 = 0x15


@dataclass
class Vertex:
    """One GS vertex (kicked by XYZ2/XYZF2)."""
    x: int = 0
    y: int = 0
    z: int = 0
    f: int = 0
    # ST + Q, or UV (mutually exclusive based on PRIM.FST)
    s: float = 0.0
    t: float = 0.0
    q: float = 1.0
    u: int = 0
    v: int = 0
    rgba: int = 0


@dataclass
class DrawCall:
    """One coherent batch of vertices with a single PRIM/TEX0 state."""
    prim: int = 0
    fst: bool = False  # PRIM.FST: 0 = use ST/Q, 1 = use UV
    tex0_1: int = 0
    tex0_2: int = 0
    tex1_1: int = 0
    tex1_2: int = 0
    vertices: List[Vertex] = field(default_factory=list)

    @property
    def prim_type(self) -> int:
        return self.prim & 7

    def prim_name(self) -> str:
        names = ['POINT', 'LINE', 'LINESTRIP', 'TRI',
                 'TRISTRIP', 'TRIFAN', 'SPRITE', 'INVALID']
        return names[self.prim_type]


def _decompress_zst(path: Path) -> bytes:
    import zstandard
    dctx = zstandard.ZstdDecompressor()
    with open(path, 'rb') as f:
        with dctx.stream_reader(f) as rdr:
            return rdr.read()


def parse_dump(path: Path) -> Tuple[bytes, List[bytes]]:
    """Return (state_bytes, [transfer_data, ...]) — the initial GS state
    blob and every Transfer packet's payload, in dump order."""
    if path.suffix == '.zst':
        raw = _decompress_zst(path)
    else:
        raw = path.read_bytes()
    p = 0
    magic = struct.unpack_from('<I', raw, p)[0]
    if magic != 0xFFFFFFFF:
        raise ValueError(f"unexpected magic {magic:#x} (old format unsupported)")
    p += 4
    header_size = struct.unpack_from('<I', raw, p)[0]
    p += 4
    # GSDumpHeader (9 u32)
    state_version, state_size, serial_offset, serial_size, crc, \
        sw, sh, ss_off, ss_size = struct.unpack_from('<9I', raw, p)
    p += 36
    serial = raw[p:p + serial_size].decode('ascii', errors='replace')
    p += serial_size
    # screenshot pixels follow (RGBA u32 per pixel)
    p += ss_size
    # state
    state = raw[p:p + state_size]
    p += state_size
    # GSPrivRegSet
    p += GS_PRIV_REG_SIZE

    print(f"[gs] serial={serial} crc={crc:08x} state_size={state_size}", file=sys.stderr)
    print(f"[gs] screenshot {sw}x{sh}, packets start at offset {p:#x}", file=sys.stderr)

    transfers: List[bytes] = []
    n_vsync = 0
    while p < len(raw):
        t = raw[p]
        p += 1
        if t == 0:  # Transfer
            path_id = raw[p]
            p += 1
            size = struct.unpack_from('<I', raw, p)[0]
            p += 4
            transfers.append(raw[p:p + size])
            p += size
        elif t == 1:  # VSync
            p += 1  # field
            n_vsync += 1
        elif t == 2:  # ReadFIFO
            p += 4
        elif t == 3:  # Registers
            p += GS_PRIV_REG_SIZE
        else:
            raise ValueError(f"unknown packet type {t} at offset {p-1:#x}")

    print(f"[gs] {len(transfers)} transfers, {n_vsync} vsyncs", file=sys.stderr)
    return state, transfers


def parse_giftag(qw0: int, qw1: int) -> Tuple[int, bool, int, int, int, List[int]]:
    """Decode a 16-byte GIFTAG. Returns (nloop, eop, prim, flg, nreg, regs)."""
    nloop = qw0 & 0x7FFF
    eop   = (qw0 >> 15) & 1
    prim  = (qw0 >> 47) & 0x7FF
    flg   = (qw0 >> 58) & 0x3
    nreg  = (qw0 >> 60) & 0xF
    if nreg == 0:
        nreg = 16
    regs  = [(qw1 >> (4 * i)) & 0xF for i in range(nreg)]
    pre   = (qw0 >> 46) & 1
    return nloop, bool(eop), prim if pre else -1, flg, nreg, regs


def _extract_vertex_writes(
    transfer: bytes,
    state: dict,
    draws: List[DrawCall],
    cur: Optional[DrawCall],
) -> Optional[DrawCall]:
    """Walk one transfer's GIF stream, accumulating vertex writes into
    DrawCall objects. ``state`` carries inter-transfer GS state."""
    p = 0
    n = len(transfer)
    while p + 16 <= n:
        qw0 = struct.unpack_from('<Q', transfer, p)[0]
        qw1 = struct.unpack_from('<Q', transfer, p + 8)[0]
        p += 16
        nloop, eop, prim, flg, nreg, regs = parse_giftag(qw0, qw1)

        if prim >= 0:
            state['prim'] = prim
            state['fst'] = bool((prim >> 8) & 1)

        if flg == 0:  # PACKED
            payload_qw = nloop * nreg
            for li in range(nloop):
                # Track partial vertex
                v = Vertex()
                v_set = False
                for ri in range(nreg):
                    if p + 16 > n:
                        return cur
                    lo = struct.unpack_from('<Q', transfer, p)[0]
                    hi = struct.unpack_from('<Q', transfer, p + 8)[0]
                    p += 16
                    r = regs[ri]
                    if r == REG_PRIM:
                        state['prim'] = lo & 0x7FF
                        state['fst'] = bool((lo >> 8) & 1)
                    elif r == REG_RGBAQ:
                        # packed: R[0..7], G[32..39], B[64..71], A[96..103]
                        # Note: PACKED RGBAQ does NOT carry Q in its data;
                        # RGBAQ.Q is set from the latched ST.Q from the
                        # most recent packed ST write — which we already
                        # placed in state['q'].
                        r_ = lo & 0xFF
                        g_ = (lo >> 32) & 0xFF
                        b_ = hi & 0xFF
                        a_ = (hi >> 32) & 0xFF
                        state['rgba'] = r_ | (g_ << 8) | (b_ << 16) | (a_ << 24)
                    elif r == REG_ST:
                        s_bits = lo & 0xFFFFFFFF
                        t_bits = (lo >> 32) & 0xFFFFFFFF
                        q_bits = hi & 0xFFFFFFFF
                        state['s'] = struct.unpack('<f', struct.pack('<I', s_bits))[0]
                        state['t'] = struct.unpack('<f', struct.pack('<I', t_bits))[0]
                        # ST also primes Q
                        state['q'] = struct.unpack('<f', struct.pack('<I', q_bits))[0]
                    elif r == REG_UV:
                        state['u'] = lo & 0x3FFF
                        state['v'] = (lo >> 16) & 0x3FFF
                    elif r in (REG_XYZF2, REG_XYZF3, REG_XYZ2, REG_XYZ3):
                        # packed XYZF: X[0..15], Y[32..47], Z[64..87], F[100..107] (XYZF only)
                        v.x = lo & 0xFFFF
                        v.y = (lo >> 32) & 0xFFFF
                        v.z = hi & 0xFFFFFF
                        v.s = state.get('s', 0.0)
                        v.t = state.get('t', 0.0)
                        v.q = state.get('q', 1.0)
                        v.u = state.get('u', 0)
                        v.v = state.get('v', 0)
                        v.rgba = state.get('rgba', 0)
                        # Determine kick: XYZ2/XYZF2 kick, XYZ3/XYZF3 don't (no draw)
                        is_kick = r in (REG_XYZF2, REG_XYZ2)
                        if is_kick:
                            if cur is None or cur.prim != state.get('prim', 0):
                                cur = DrawCall(
                                    prim=state.get('prim', 0),
                                    fst=state.get('fst', False),
                                    tex0_1=state.get('tex0_1', 0),
                                    tex0_2=state.get('tex0_2', 0),
                                    tex1_1=state.get('tex1_1', 0),
                                    tex1_2=state.get('tex1_2', 0),
                                )
                                draws.append(cur)
                            cur.vertices.append(v)
                            v_set = True
                    elif r == REG_TEX0_1:
                        state['tex0_1'] = lo
                        cur = None  # state changed → flush draw grouping
                    elif r == REG_TEX0_2:
                        state['tex0_2'] = lo
                        cur = None
                    elif r == REG_AD:
                        addr = hi & 0xFF
                        if addr == ADDR_PRIM:
                            state['prim'] = lo & 0x7FF
                            state['fst'] = bool((lo >> 8) & 1)
                        elif addr == ADDR_TEX0_1:
                            state['tex0_1'] = lo
                            cur = None
                        elif addr == ADDR_TEX0_2:
                            state['tex0_2'] = lo
                            cur = None
                        elif addr == ADDR_TEX1_1:
                            state['tex1_1'] = lo
                            cur = None
                        elif addr == ADDR_TEX1_2:
                            state['tex1_2'] = lo
                            cur = None
                        elif addr == ADDR_ST:
                            state['s'] = struct.unpack('<f', struct.pack('<Q', lo)[:4])[0]
                            state['t'] = struct.unpack('<f', struct.pack('<Q', lo)[4:])[0]
                        elif addr == ADDR_UV:
                            state['u'] = lo & 0x3FFF
                            state['v'] = (lo >> 16) & 0x3FFF
                        elif addr == ADDR_XYZ2 or addr == ADDR_XYZF2:
                            v.x = lo & 0xFFFF
                            v.y = (lo >> 16) & 0xFFFF
                            v.z = (lo >> 32) & 0xFFFFFFFF
                            v.s = state.get('s', 0.0)
                            v.t = state.get('t', 0.0)
                            v.q = state.get('q', 1.0)
                            v.u = state.get('u', 0)
                            v.v = state.get('v', 0)
                            v.rgba = state.get('rgba', 0)
                            if cur is None or cur.prim != state.get('prim', 0):
                                cur = DrawCall(
                                    prim=state.get('prim', 0),
                                    fst=state.get('fst', False),
                                    tex0_1=state.get('tex0_1', 0),
                                )
                                draws.append(cur)
                            cur.vertices.append(v)
        elif flg == 1:  # REGLIST: NLOOP*NREG 64-bit values, padded to QW
            count = nloop * nreg
            qw_count = (count + 1) // 2
            p += qw_count * 16
        else:  # IMAGE (FLG=2/3): NLOOP * 16 raw bytes
            p += nloop * 16

        if eop:
            # End of packet — but data may still follow (next GIFTAG)
            pass
    return cur


def extract_drawcalls(path: Path) -> List[DrawCall]:
    state, transfers = parse_dump(path)
    draws: List[DrawCall] = []
    cur: Optional[DrawCall] = None
    gs_state: dict = {}
    for ti, t in enumerate(transfers):
        cur = _extract_vertex_writes(t, gs_state, draws, cur)
    return draws


def _decode_tex0(tex0: int) -> dict:
    return {
        'tbp':  tex0 & 0x3FFF,
        'tbw':  (tex0 >> 14) & 0x3F,
        'psm':  (tex0 >> 20) & 0x3F,
        'tw':   1 << ((tex0 >> 26) & 0xF),
        'th':   1 << ((tex0 >> 30) & 0xF),
    }


def main() -> None:
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('path', type=Path)
    ap.add_argument('--limit', type=int, default=20,
                    help='print at most N draw calls')
    ap.add_argument('--min-verts', type=int, default=4,
                    help='only show draws with at least N vertices')
    args = ap.parse_args()

    draws = extract_drawcalls(args.path)
    print(f"total draws: {len(draws)}")
    big = [d for d in draws if len(d.vertices) >= args.min_verts]
    print(f"with >= {args.min_verts} verts: {len(big)}")

    for di, d in enumerate(big[:args.limit]):
        t0 = _decode_tex0(d.tex0_1)
        print(f"\ndraw {di}: {d.prim_name()} verts={len(d.vertices)} fst={d.fst} "
              f"tex0={d.tex0_1:016x} (tbp={t0['tbp']:#x} {t0['tw']}x{t0['th']})")
        for vi, v in enumerate(d.vertices[:6]):
            # XYZ are 12.4 fixed (with 0x8000 offset by GS spec)
            x = (v.x - 0x0000) / 16.0
            y = (v.y - 0x0000) / 16.0
            if d.fst:
                print(f"  v{vi}: xy=({x:8.2f},{y:8.2f}) z={v.z:08x} uv=({v.u},{v.v})")
            else:
                # Effective UV = ST/Q scaled by texture size
                if v.q != 0:
                    u_eff = v.s / v.q * t0['tw']
                    v_eff = v.t / v.q * t0['th']
                else:
                    u_eff = v_eff = 0.0
                print(f"  v{vi}: xy=({x:8.2f},{y:8.2f}) z={v.z:08x} "
                      f"st=({v.s:.4f},{v.t:.4f}) q={v.q:.4f} -> uv=({u_eff:.2f},{v_eff:.2f})")


if __name__ == '__main__':
    main()
