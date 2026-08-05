#!/usr/bin/env python3
"""Disassembler for PS2 VU micro memory images.

Input is a raw dump of VU0 (4 KB) or VU1 (16 KB) micro memory, such as the
`vu1MicroMem.bin` PCSX2 writes into a save state directory. Each instruction is
8 bytes: the low word is the LOWER (integer/load/store/branch) instruction and
the high word is the UPPER (FMAC) instruction.

Field extraction is the documented layout, confirmed against PCSX2's macros:

    _Ft_  = (code >> 16) & 0x1F        dest mask bit 24 = X
    _Fs_  = (code >> 11) & 0x1F                     23 = Y
    _Fd_  = (code >>  6) & 0x1F                     22 = Z
    _Fsf_ = (code >> 21) & 0x03                     21 = W
    _Ftf_ = (code >> 23) & 0x03

    upper opcode = code & 0x3F, with 0x3C..0x3F dispatching through a
                   sub-table indexed by _Fd_
    lower opcode = code >> 25, with 0x40 dispatching through a sub-table
                   indexed by code & 0x3F, whose 0x3C..0x3F entries dispatch
                   again through a sub-table indexed by _Fd_

Upper-instruction flag bits: 31 = I (the lower word of this instruction is a
32-bit float immediate loaded into I, and the lower slot does not execute),
30 = E (last instruction of the microprogram; two more issue after it),
29 = M, 28 = D, 27 = T.

Every table below has been checked element-by-element against PCSX2's literal
dispatch arrays in pcsx2/x86/microVU_Tables.inl (mVULOWER_OPCODE,
mVU_UPPER_OPCODE, mVULowerOP_OPCODE, the four mVULowerOP_T3_* and the four
mVU_UPPER_FD_*_TABLE). Two entries were wrong before that check and are worth
knowing about if you read output produced by an earlier revision: EEXP is
T3_10 index 31, not T3_11 index 30, and T3_11 index 30 is WAITP. That
mis-decode turned the ordinary `ELENG / WAITP / MFP` idiom into a nonsensical
`EEXP P, vf00x`.
"""

import argparse
import struct
import sys

# ---------------------------------------------------------------- upper table

_BC = "xyzw"

# index = code & 0x3F
UPPER = {}
for _i, _bc in enumerate(_BC):
    UPPER[0x00 + _i] = ("ADD" + _bc, "bc")
    UPPER[0x04 + _i] = ("SUB" + _bc, "bc")
    UPPER[0x08 + _i] = ("MADD" + _bc, "bc")
    UPPER[0x0C + _i] = ("MSUB" + _bc, "bc")
    UPPER[0x10 + _i] = ("MAX" + _bc, "bc")
    UPPER[0x14 + _i] = ("MINI" + _bc, "bc")
    UPPER[0x18 + _i] = ("MUL" + _bc, "bc")
UPPER.update({
    0x1C: ("MULq", "fdfs_q"),
    0x1D: ("MAXi", "fdfs_i"),
    0x1E: ("MULi", "fdfs_i"),
    0x1F: ("MINIi", "fdfs_i"),
    0x20: ("ADDq", "fdfs_q"),
    0x21: ("MADDq", "fdfs_q"),
    0x22: ("ADDi", "fdfs_i"),
    0x23: ("MADDi", "fdfs_i"),
    0x24: ("SUBq", "fdfs_q"),
    0x25: ("MSUBq", "fdfs_q"),
    0x26: ("SUBi", "fdfs_i"),
    0x27: ("MSUBi", "fdfs_i"),
    0x28: ("ADD", "fdfsft"),
    0x29: ("MADD", "fdfsft"),
    0x2A: ("MUL", "fdfsft"),
    0x2B: ("MAX", "fdfsft"),
    0x2C: ("SUB", "fdfsft"),
    0x2D: ("MSUB", "fdfsft"),
    0x2E: ("OPMSUB", "fdfsft"),
    0x2F: ("MINI", "fdfsft"),
})

# The 0x3C..0x3F sub-tables, indexed by _Fd_. "A" forms write the accumulator
# and so take no fd operand.
UPPER_FD = {
    0x3C: {
        0x00: ("ADDAx", "acc_bc"), 0x01: ("SUBAx", "acc_bc"),
        0x02: ("MADDAx", "acc_bc"), 0x03: ("MSUBAx", "acc_bc"),
        0x04: ("ITOF0", "fdfs"), 0x05: ("FTOI0", "fdfs"),
        0x06: ("MULAx", "acc_bc"), 0x07: ("MULAq", "acc_q"),
        0x08: ("ADDAq", "acc_q"), 0x09: ("SUBAq", "acc_q"),
        0x0A: ("ADDA", "acc_ft"), 0x0B: ("SUBA", "acc_ft"),
    },
    0x3D: {
        0x00: ("ADDAy", "acc_bc"), 0x01: ("SUBAy", "acc_bc"),
        0x02: ("MADDAy", "acc_bc"), 0x03: ("MSUBAy", "acc_bc"),
        0x04: ("ITOF4", "fdfs"), 0x05: ("FTOI4", "fdfs"),
        0x06: ("MULAy", "acc_bc"), 0x07: ("ABS", "fdfs"),
        0x08: ("MADDAq", "acc_q"), 0x09: ("MSUBAq", "acc_q"),
        0x0A: ("MADDA", "acc_ft"), 0x0B: ("MSUBA", "acc_ft"),
    },
    0x3E: {
        0x00: ("ADDAz", "acc_bc"), 0x01: ("SUBAz", "acc_bc"),
        0x02: ("MADDAz", "acc_bc"), 0x03: ("MSUBAz", "acc_bc"),
        0x04: ("ITOF12", "fdfs"), 0x05: ("FTOI12", "fdfs"),
        0x06: ("MULAz", "acc_bc"), 0x07: ("MULAi", "acc_i"),
        0x08: ("ADDAi", "acc_i"), 0x09: ("SUBAi", "acc_i"),
        0x0A: ("MULA", "acc_ft"), 0x0B: ("OPMULA", "acc_ft"),
    },
    0x3F: {
        0x00: ("ADDAw", "acc_bc"), 0x01: ("SUBAw", "acc_bc"),
        0x02: ("MADDAw", "acc_bc"), 0x03: ("MSUBAw", "acc_bc"),
        0x04: ("ITOF15", "fdfs"), 0x05: ("FTOI15", "fdfs"),
        0x06: ("MULAw", "acc_bc"), 0x07: ("CLIP", "clip"),
        0x08: ("MADDAi", "acc_i"), 0x09: ("MSUBAi", "acc_i"),
        0x0B: ("NOP", "none"),
    },
}

# ---------------------------------------------------------------- lower table

# index = code >> 25
LOWER = {
    0x00: ("LQ", "lq"),
    0x01: ("SQ", "sq"),
    0x04: ("ILW", "ilw"),
    0x05: ("ISW", "isw"),
    0x08: ("IADDIU", "iaddiu"),
    0x09: ("ISUBIU", "iaddiu"),
    0x10: ("FCEQ", "fc_imm24"),
    0x11: ("FCSET", "fc_imm24"),
    0x12: ("FCAND", "fc_imm24"),
    0x13: ("FCOR", "fc_imm24"),
    0x14: ("FSEQ", "fs_imm12"),
    0x15: ("FSSET", "fs_imm12"),
    0x16: ("FSAND", "fs_imm12"),
    0x17: ("FSOR", "fs_imm12"),
    0x18: ("FMEQ", "it_is"),
    0x1A: ("FMAND", "it_is"),
    0x1B: ("FMOR", "it_is"),
    0x1C: ("FCGET", "it_only"),
    0x20: ("B", "branch"),
    0x21: ("BAL", "branch_it"),
    0x24: ("JR", "jr"),
    0x25: ("JALR", "jalr"),
    0x28: ("IBEQ", "branch2"),
    0x29: ("IBNE", "branch2"),
    0x2C: ("IBLTZ", "branch1"),
    0x2D: ("IBGTZ", "branch1"),
    0x2E: ("IBLEZ", "branch1"),
    0x2F: ("IBGEZ", "branch1"),
}

# index = code & 0x3F, reached when (code >> 25) == 0x40
LOWER_OP = {
    0x30: ("IADD", "idst"),
    0x31: ("ISUB", "idst"),
    0x32: ("IADDI", "iaddi"),
    0x34: ("IAND", "idst"),
    0x35: ("IOR", "idst"),
}

# The 0x3C..0x3F sub-tables of LOWER_OP, indexed by _Fd_.
LOWER_T3 = {
    0x3C: {
        0x0C: ("MOVE", "fdfs"), 0x0D: ("LQI", "lqi"),
        0x0E: ("DIV", "div"), 0x0F: ("MTIR", "mtir"),
        0x10: ("RNEXT", "rnext"),
        0x19: ("MFP", "mfp"), 0x1A: ("XTOP", "it_only"),
        0x1B: ("XGKICK", "is_only"),
        0x1C: ("ESADD", "efu"), 0x1D: ("EATANxy", "efu"),
        0x1E: ("ESQRT", "efu_f"), 0x1F: ("ESIN", "efu_f"),
    },
    0x3D: {
        0x0C: ("MR32", "fdfs"), 0x0D: ("SQI", "sqi"),
        0x0E: ("SQRT", "sqrt"), 0x0F: ("MFIR", "mfir"),
        0x10: ("RGET", "rget"),
        0x1A: ("XITOP", "it_only"),
        0x1C: ("ERSADD", "efu"), 0x1D: ("EATANxz", "efu"),
        0x1E: ("ERSQRT", "efu_f"), 0x1F: ("EATAN", "efu_f"),
    },
    0x3E: {
        0x0D: ("LQD", "lqd"), 0x0E: ("RSQRT", "div"), 0x0F: ("ILWR", "ilwr"),
        0x10: ("RINIT", "rinit"),
        0x1C: ("ELENG", "efu"), 0x1D: ("ESUM", "efu"),
        0x1E: ("ERCPR", "efu_f"), 0x1F: ("EEXP", "efu_f"),
    },
    0x3F: {
        0x0D: ("SQD", "sqd"), 0x0E: ("WAITQ", "none"), 0x0F: ("ISWR", "iswr"),
        0x10: ("RXOR", "rinit"),
        0x1C: ("ERLENG", "efu"), 0x1E: ("WAITP", "none"),
    },
}

# ------------------------------------------------------------------- decoding


def _dest(code):
    mask = ""
    for bit, name in ((24, "x"), (23, "y"), (22, "z"), (21, "w")):
        if (code >> bit) & 1:
            mask += name
    return mask


def _s(bits, width):
    sign = 1 << (width - 1)
    return (bits ^ sign) - sign


def decode_upper(code):
    op = code & 0x3F
    fd, fs, ft = (code >> 6) & 0x1F, (code >> 11) & 0x1F, (code >> 16) & 0x1F
    d = _dest(code)

    if op in UPPER_FD:
        entry = UPPER_FD[op].get(fd)
        if entry is None:
            return "<upper %02x/%02x?>" % (op, fd)
        name, form = entry
    else:
        entry = UPPER.get(op)
        if entry is None:
            return "<upper %02x?>" % op
        name, form = entry

    bc = _BC[op & 3]
    if form == "bc":
        return "%s.%s vf%02d, vf%02d, vf%02d%s" % (name, d, fd, fs, ft, bc)
    if form == "acc_bc":
        return "%s.%s ACC, vf%02d, vf%02d%s" % (name, d, fs, ft, bc)
    if form == "fdfsft":
        return "%s.%s vf%02d, vf%02d, vf%02d" % (name, d, fd, fs, ft)
    if form == "acc_ft":
        return "%s.%s ACC, vf%02d, vf%02d" % (name, d, fs, ft)
    if form in ("fdfs_q", "fdfs_i"):
        return "%s.%s vf%02d, vf%02d, %s" % (name, d, fd, fs, form[-1].upper())
    if form in ("acc_q", "acc_i"):
        return "%s.%s ACC, vf%02d, %s" % (name, d, fs, form[-1].upper())
    if form == "fdfs":
        # In the 0x3C..0x3F sub-tables the fd field is the sub-opcode, so these
        # write ft and read fs.
        return "%s.%s vf%02d, vf%02d" % (name, d, ft, fs)
    if form == "clip":
        return "CLIP vf%02dxyz, vf%02dw" % (fs, ft)
    if form == "none":
        return "NOP"
    return "%s?" % name


def decode_lower(code, pc):
    top = code >> 25
    fd, fs, ft = (code >> 6) & 0x1F, (code >> 11) & 0x1F, (code >> 16) & 0x1F
    it, is_, id_ = ft & 0xF, fs & 0xF, fd & 0xF
    d = _dest(code)
    imm11 = _s(code & 0x7FF, 11)
    target = (pc + 1 + imm11) & 0x3FF if False else pc + 1 + imm11

    if top == 0x40:
        sub = code & 0x3F
        if sub in LOWER_T3:
            entry = LOWER_T3[sub].get(fd)
            if entry is None:
                # Dest-mask-zero MOVE is the canonical lower NOP.
                return "<lower %02x/%02x?>" % (sub, fd)
            name, form = entry
        else:
            entry = LOWER_OP.get(sub)
            if entry is None:
                return "<lower op %02x?>" % sub
            name, form = entry

        ftf, fsf = (code >> 23) & 3, (code >> 21) & 3
        if form == "fdfs":
            # Same as the upper sub-tables: fd is the sub-opcode, so MOVE/MR32
            # write ft and read fs. A null dest mask is the canonical lower NOP.
            if not d and name == "MOVE":
                return "NOP"
            return "%s.%s vf%02d, vf%02d" % (name, d, ft, fs)
        if form == "idst":
            return "%s vi%02d, vi%02d, vi%02d" % (name, id_, is_, it)
        if form == "iaddi":
            return "IADDI vi%02d, vi%02d, %d" % (it, is_, _s((code >> 6) & 0x1F, 5))
        if form == "lqi":
            return "LQI.%s vf%02d, (vi%02d++)" % (d, ft, is_)
        if form == "lqd":
            return "LQD.%s vf%02d, (--vi%02d)" % (d, ft, is_)
        if form == "sqi":
            return "SQI.%s vf%02d, (vi%02d++)" % (d, fs, it)
        if form == "sqd":
            return "SQD.%s vf%02d, (--vi%02d)" % (d, fs, it)
        if form == "div":
            return "%s Q, vf%02d%s, vf%02d%s" % (name, fs, _BC[fsf], ft, _BC[ftf])
        if form == "sqrt":
            return "SQRT Q, vf%02d%s" % (ft, _BC[ftf])
        if form == "mtir":
            return "MTIR vi%02d, vf%02d%s" % (it, fs, _BC[fsf])
        if form == "mfir":
            return "MFIR.%s vf%02d, vi%02d" % (d, ft, is_)
        if form == "ilwr":
            return "ILWR.%s vi%02d, (vi%02d)" % (d, it, is_)
        if form == "iswr":
            return "ISWR.%s vi%02d, (vi%02d)" % (d, it, is_)
        if form == "it_only":
            return "%s vi%02d" % (name, it)
        if form == "is_only":
            return "%s vi%02d" % (name, is_)
        if form == "mfp":
            return "MFP.%s vf%02d, P" % (d, ft)
        if form == "efu":
            return "%s P, vf%02d" % (name, fs)
        if form == "efu_f":
            return "%s P, vf%02d%s" % (name, fs, _BC[fsf])
        if form in ("rnext", "rget"):
            return "%s.%s vf%02d, R" % (name, d, ft)
        if form == "rinit":
            return "%s R, vf%02d%s" % (name, fs, _BC[fsf])
        if form == "none":
            return name
        return "%s?" % name

    entry = LOWER.get(top)
    if entry is None:
        return "<lower %02x?>" % top
    name, form = entry
    imm15 = ((code >> 10) & 0x7800) | (code & 0x7FF)

    if form == "lq":
        return "LQ.%s vf%02d, %d(vi%02d)" % (d, ft, imm11, is_)
    if form == "sq":
        return "SQ.%s vf%02d, %d(vi%02d)" % (d, fs, imm11, it)
    if form == "ilw":
        return "ILW.%s vi%02d, %d(vi%02d)" % (d, it, imm11, is_)
    if form == "isw":
        return "ISW.%s vi%02d, %d(vi%02d)" % (d, it, imm11, is_)
    if form == "iaddiu":
        return "%s vi%02d, vi%02d, 0x%x" % (name, it, is_, imm15)
    if form == "branch":
        return "B %d          ; -> %04x" % (imm11, target)
    if form == "branch_it":
        return "BAL vi%02d, %d    ; -> %04x" % (it, imm11, target)
    if form == "branch1":
        return "%s vi%02d, %d  ; -> %04x" % (name, is_, imm11, target)
    if form == "branch2":
        return "%s vi%02d, vi%02d, %d  ; -> %04x" % (name, it, is_, imm11, target)
    if form == "jr":
        return "JR vi%02d" % is_
    if form == "jalr":
        return "JALR vi%02d, vi%02d" % (it, is_)
    if form == "fc_imm24":
        return "%s 0x%06x" % (name, code & 0xFFFFFF)
    if form == "fs_imm12":
        return "%s vi%02d, 0x%03x" % (name, it, ((code >> 10) & 0x800) | (code & 0x7FF))
    if form == "it_is":
        return "%s vi%02d, vi%02d" % (name, it, is_)
    if form == "it_only":
        return "%s vi%02d" % (name, it)
    return "%s?" % name


def branch_target(code, pc):
    """Instruction index this lower word branches to, or None."""
    top = code >> 25
    if top in (0x20, 0x21, 0x28, 0x29, 0x2C, 0x2D, 0x2E, 0x2F):
        return pc + 1 + _s(code & 0x7FF, 11)
    return None


def disassemble(data, start=0, end=None, labels=None):
    count = len(data) // 8
    end = count if end is None else min(end, count)
    labels = labels or {}
    out = []
    pc = start
    while pc < end:
        lo, up = struct.unpack_from("<II", data, pc * 8)
        flags = "".join(n for b, n in ((31, "I"), (30, "E"), (29, "M"),
                                       (28, "D"), (27, "T")) if (up >> b) & 1)
        if pc in labels:
            out.append("")
            out.append("%s:" % labels[pc])
        if (up >> 31) & 1:
            lower = "LOI %.9g" % struct.unpack("<f", struct.pack("<I", lo))[0]
        else:
            lower = decode_lower(lo, pc)
        out.append("%04x  %08x %08x  %-3s %-34s %s"
                   % (pc, up, lo, flags, decode_upper(up), lower))
        pc += 1
    return "\n".join(out)


def trace_program(data, entry, limit=4096):
    """Instructions reachable from `entry`, following branches until every path
    has hit an E-bit instruction or left the image."""
    count = len(data) // 8
    seen, work = set(), [entry]
    while work:
        pc = work.pop()
        while 0 <= pc < count and pc not in seen and len(seen) < limit:
            seen.add(pc)
            lo, up = struct.unpack_from("<II", data, pc * 8)
            if not (up >> 31) & 1:
                tgt = branch_target(lo, pc)
                if tgt is not None:
                    work.append(tgt)
                    if (lo >> 25) in (0x20, 0x24, 0x25):  # unconditional
                        break
            if (up >> 30) & 1:  # E bit: two more instructions issue, then stop
                seen.add(pc + 1)
                seen.add(pc + 2)
                break
            pc += 1
    return sorted(p for p in seen if p < count)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", help="VU micro memory dump")
    ap.add_argument("--start", type=lambda s: int(s, 0), default=0)
    ap.add_argument("--end", type=lambda s: int(s, 0), default=None)
    ap.add_argument("--entry", type=lambda s: int(s, 0), action="append",
                    help="trace reachable instructions from this entry point")
    ap.add_argument("--map", action="store_true",
                    help="report E-bit instructions and branch targets only")
    args = ap.parse_args()

    data = open(args.image, "rb").read()
    count = len(data) // 8

    if args.map:
        ends, targets = [], set()
        for pc in range(count):
            lo, up = struct.unpack_from("<II", data, pc * 8)
            if (up >> 30) & 1:
                ends.append(pc)
            if not (up >> 31) & 1:
                t = branch_target(lo, pc)
                if t is not None:
                    targets.add(t)
        print("%d instructions" % count)
        print("E-bit (program ends) at: %s" % " ".join("%04x" % e for e in ends))
        print("branch targets: %s" % " ".join("%04x" % t for t in sorted(targets)))
        return

    if args.entry:
        for entry in args.entry:
            reached = trace_program(data, entry)
            print("; ===== entry %04x: %d instructions reachable, %04x..%04x"
                  % (entry, len(reached), reached[0], reached[-1]))
            print(disassemble(data, reached[0], reached[-1] + 1))
            print()
        return

    print(disassemble(data, args.start, args.end))


if __name__ == "__main__":
    sys.exit(main())
