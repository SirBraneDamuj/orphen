import struct
import sys

def decode_insn(pc, word):
    op = (word >> 26) & 0x3F
    rs = (word >> 21) & 0x1F
    rt = (word >> 16) & 0x1F
    rd = (word >> 11) & 0x1F
    shamt = (word >> 6) & 0x1F
    funct = word & 0x3F
    imm = word & 0xFFFF
    imm_signed = imm if imm < 0x8000 else imm - 0x10000
    target = word & 0x03FFFFFF

    regs = ["zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
            "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
            "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
            "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]

    if op == 0:
        if funct == 0x20: name = "add"
        elif funct == 0x21: name = "addu"
        elif funct == 0x24: name = "and"
        elif funct == 0x08: name = "jr"
        elif funct == 0x09: name = "jalr"
        elif funct == 0x25: name = "or"
        elif funct == 0x26: name = "xor"
        elif funct == 0x27: name = "nor"
        elif funct == 0x2a: name = "slt"
        elif funct == 0x2b: name = "sltu"
        elif funct == 0x00: name = "sll"
        elif funct == 0x02: name = "srl"
        elif funct == 0x03: name = "sra"
        elif funct == 0x12: name = "mflo"
        elif funct == 0x10: name = "mfhi"
        else: name = f"spec_{funct:02x}"
        
        if name in ["sll", "srl", "sra"]:
            return f"{name} {regs[rd]}, {regs[rt]}, {shamt}"
        elif name in ["jr", "jalr"]:
            return f"{name} {regs[rs]}"
        elif name in ["mflo", "mfhi"]:
            return f"{name} {regs[rd]}"
        else:
            return f"{name} {regs[rd]}, {regs[rs]}, {regs[rt]}"
    elif op == 0x0c:
        return f"andi {regs[rt]}, {regs[rs]}, {imm:#06x}"
    elif op == 0x0d:
        return f"ori {regs[rt]}, {regs[rs]}, {imm:#06x}"
    elif op == 0x0e:
        return f"xori {regs[rt]}, {regs[rs]}, {imm:#06x}"
    elif op == 0x0f:
        return f"lui {regs[rt]}, {imm:#06x}"
    elif op == 0x08:
        return f"addi {regs[rt]}, {regs[rs]}, {imm_signed}"
    elif op == 0x09:
        return f"addiu {regs[rt]}, {regs[rs]}, {imm_signed}"
    elif op == 0x0a:
        return f"slti {regs[rt]}, {regs[rs]}, {imm_signed}"
    elif op == 0x0b:
        return f"sltiu {regs[rt]}, {regs[rs]}, {imm_signed}"
    elif op == 0x23:
        return f"lw {regs[rt]}, {imm_signed:#x}({regs[rs]})"
    elif op == 0x2b:
        return f"sw {regs[rt]}, {imm_signed:#x}({regs[rs]})"
    elif op == 0x21:
        return f"lh {regs[rt]}, {imm_signed:#x}({regs[rs]})"
    elif op == 0x25:
        return f"lhu {regs[rt]}, {imm_signed:#x}({regs[rs]})"
    elif op == 0x20:
        return f"lb {regs[rt]}, {imm_signed:#x}({regs[rs]})"
    elif op == 0x24:
        return f"lbu {regs[rt]}, {imm_signed:#x}({regs[rs]})"
    elif op == 0x04:
        target_pc = pc + 4 + (imm_signed << 2)
        return f"beq {regs[rs]}, {regs[rt]}, {target_pc:#x}"
    elif op == 0x05:
        target_pc = pc + 4 + (imm_signed << 2)
        return f"bne {regs[rs]}, {regs[rt]}, {target_pc:#x}"
    elif op == 0x02:
        target_pc = (pc & 0xF0000000) | (target << 2)
        return f"j {target_pc:#x}"
    elif op == 0x03:
        target_pc = (pc & 0xF0000000) | (target << 2)
        return f"jal {target_pc:#x}"
    else:
        return f"dw {word:#010x} (op {op:#04x})"

def main():
    with open('SLUS_200.11', 'rb') as f:
        vaddr_start = 0x200000
        offset_start = 0x1000
        
        def get_word(addr):
            off = addr - vaddr_start + offset_start
            f.seek(off)
            return struct.unpack('<I', f.read(4))[0]

        ranges = [(0x00227930, 0x00227984), (0x00227b80, 0x00227c24)]
        for start, end in ranges:
            print(f"--- Range {start:#x} - {end:#x} ---")
            for addr in range(start, end, 4):
                word = get_word(addr)
                print(f"{addr:#x}: {word:08x}  {decode_insn(addr, word)}")

        print("\n--- Scanning 0x00227840 - 0x00227d10 ---")
        scan_start = 0x00227840
        scan_end = 0x00227d14
        words = []
        for addr in range(scan_start, scan_end, 4):
            words.append((addr, get_word(addr)))
        
        for i, (addr, word) in enumerate(words):
            op = (word >> 26) & 0x3F
            rs = (word >> 21) & 0x1F
            rt = (word >> 16) & 0x1F
            rd = (word >> 11) & 0x1F
            funct = word & 0x3F
            imm = word & 0xFFFF
            
            # andi rt, rs, 0x0800
            if op == 0x0c and imm == 0x0800:
                print(f"MATCH andi: {addr:#x}: {word:08x}  {decode_insn(addr, word)}")
            
            # lw ?, 0x004c($s0)
            if op == 0x23 and rs == 16 and imm == 0x4c:
                print(f"FOUND lw: {addr:#x}: {word:08x}  {decode_insn(addr, word)}")
                # Scan next 8 instructions
                for j in range(1, 9):
                    if i + j >= len(words): break
                    n_addr, n_word = words[i+j]
                    n_op = (n_word >> 26) & 0x3F
                    n_funct = n_word & 0x3F
                    if n_op == 0 and n_funct == 0x24: # and
                        print(f"  + within 8: {n_addr:#x}: {n_word:08x}  {decode_insn(n_addr, n_word)}")
                    if n_op in [0x04, 0x05]: # beq, bne
                        print(f"  + within 8: {n_addr:#x}: {n_word:08x}  {decode_insn(n_addr, n_word)}")

main()
