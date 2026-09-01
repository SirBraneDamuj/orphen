"""Walk a PCSX2 .gs packet stream and rebuild the draws with their GS state."""
import struct

PACKED, REGLIST, IMAGE, DISABLE = 0, 1, 2, 3
PRIM_NAMES = ['point', 'line', 'linestrip', 'tri', 'tristrip', 'trifan', 'sprite', 'bad']


def find_stream(d):
    """The header arithmetic does not land exactly; scan for the offset whose
    packet walk consumes the file to the last byte."""
    def ok(start):
        q, n = start, 0
        while q < len(d):
            pid = d[q]; q += 1
            if pid == 0:
                if q + 5 > len(d): return None
                path = d[q]; q += 1
                sz = struct.unpack_from('<I', d, q)[0]; q += 4
                if sz > 8_000_000 or path > 3: return None
                q += sz
            elif pid == 1: q += 1
            elif pid == 2: q += 4
            elif pid == 3: q += 8192
            else: return None
            n += 1
            if q > len(d): return None
        return n
    hdr = struct.unpack_from('<9I', d, 8)
    guess = 8 + 36 + struct.unpack_from('<I', d, 4)[0] + hdr[1]
    for start in range(max(0, guess - 8000), guess + 20000):
        if ok(start): return start
    raise RuntimeError('no packet stream found')


class GS:
    def __init__(self):
        self.reg = {}          # A+D register writes, addr -> u64
        self.prim = 0
        self.rgbaq = (0, 0, 0, 0)
        self.uv = (0, 0)
        self.draws = []        # one entry per completed vertex batch
        self.pending = []
        self.at_start = None
        self.frame = 0

    def xyoffset(self):
        v = self.reg.get(0x18, 0)
        return (v & 0xFFFF) / 16.0, ((v >> 32) & 0xFFFF) / 16.0

    def flush(self):
        if self.pending:
            # The register state that matters is the one in force when the batch
            # *started*. VU1 emits the next draw's A+D block (ALPHA, ZBUF) ahead
            # of its GIF tag, so by the time the previous batch is flushed the
            # registers already describe the draw after it -- reading them here
            # shifts every blend mode by one draw.
            ofx, ofy = self.at_start['xyoffset']
            self.draws.append(dict(
                frame=self.frame, prim=self.prim,
                tex0=self.at_start['tex0'], alpha=self.at_start['alpha'],
                test=self.at_start['test'], zbuf=self.at_start['zbuf'],
                verts=[(x / 16.0 - ofx, y / 16.0 - ofy, z, c, uv) for x, y, z, c, uv in self.pending]))
            self.pending = []

    def snapshot(self):
        return dict(xyoffset=self.xyoffset(), tex0=self.reg.get(0x06, 0),
                    alpha=self.reg.get(0x42, 0), test=self.reg.get(0x47, 0),
                    zbuf=self.reg.get(0x4E, 0))

    def vertex(self, x, y, z):
        if not self.pending:
            self.at_start = self.snapshot()
        self.pending.append((x, y, z, self.rgbaq, self.uv))


def walk(d, start):
    gs = GS()
    q = len(d)
    p = start
    while p < len(d):
        pid = d[p]; p += 1
        if pid == 1:      # VSync
            gs.flush(); gs.frame += 1; p += 1; continue
        if pid == 2:
            p += 4; continue
        if pid == 3:
            p += 8192; continue
        path = d[p]; p += 1
        n = struct.unpack_from('<I', d, p)[0]; p += 4
        blob = d[p:p + n]; p += n
        feed(gs, blob)
    gs.flush()
    return gs


def feed(gs, b):
    o = 0
    while o + 16 <= len(b):
        lo, hi = struct.unpack_from('<QQ', b, o); o += 16
        nloop = lo & 0x7FFF
        eop = (lo >> 15) & 1
        pre = (lo >> 46) & 1
        prim = (lo >> 47) & 0x7FF
        flg = (lo >> 58) & 3
        nreg = (lo >> 60) & 0xF or 16
        regs = hi
        if pre:
            gs.flush(); gs.prim = prim
        if flg == IMAGE:
            o += nloop * 16
            continue
        if flg == DISABLE:
            continue
        if flg == REGLIST:
            o += ((nloop * nreg + 1) // 2) * 16
            continue
        for _ in range(nloop):
            for r in range(nreg):
                if o + 16 > len(b): return
                reg = (regs >> (4 * r)) & 0xF
                w = struct.unpack_from('<4I', b, o); o += 16
                if reg == 0x00:
                    gs.flush(); gs.prim = w[0] & 0x7FF
                elif reg == 0x01:
                    gs.rgbaq = (w[0] & 0xFF, w[1] & 0xFF, w[2] & 0xFF, w[3] & 0xFF)
                elif reg == 0x03:
                    gs.uv = (w[0] & 0x3FFF, w[1] & 0x3FFF)
                elif reg == 0x04:       # XYZF2
                    gs.vertex(w[0] & 0xFFFF, w[1] & 0xFFFF, (w[2] >> 4) | ((w[3] & 0xF) << 28))
                elif reg == 0x05:       # XYZ2
                    gs.vertex(w[0] & 0xFFFF, w[1] & 0xFFFF, w[2])
                elif reg == 0x0E:       # A+D
                    gs.reg[w[2] & 0xFF] = w[0] | (w[1] << 32)
