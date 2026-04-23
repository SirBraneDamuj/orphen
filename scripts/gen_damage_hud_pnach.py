"""
Generate battle_damage_hud.pnach - displays attack/target info on hit.

Memory layout (all in unused EE RAM between .bss end and stack):
  0x01E00000: Display hook (per-frame, replaces jal FUN_00268270 at 0x00200844)
  0x01E00100: hex4 subroutine (converts 16-bit value to 4 ASCII hex chars)
  0x01E00180: Hex lookup table "0123456789ABCDEF"
  0x01E00140: hex2 subroutine (converts 8-bit value to 2 ASCII hex chars)
  0x01E00200: Damage capture hook (replaces jal FUN_002d5630 at 0x002163f8)
  0x01E00500: Runtime data (timer, NOT patched by pnach - persists across frames)
  0x01E00600: Display line 1 buffer (NOT patched - written by damage hook)
  0x01E00640: Display line 2 buffer (NOT patched - written by damage hook)
  0x01E00680: Display line 3 buffer (NOT patched - effectiveness P/L/W)
  0x01E006C0: Display line 4 buffer (NOT patched - effectiveness F/D/I)

Display format (when a hit triggers the HP bar):
  Line 1: "E:XXXX T:XXXX >XXXX"  (attack element, damage type, net damage)
  Line 2: "Df:XXXX HP:XXXX/XXXX" (defense, current HP / max HP)
  Line 3: "P:XX L:XX W:XX"       (physical, lightning, wind effectiveness)
  Line 4: "F:XX D:XX I:XX"       (fire, dark, ice effectiveness)
"""
import struct

# --- MIPS encoding helpers ---
ZERO, A0, A1, A2, A3 = 0, 4, 5, 6, 7
T0, T1, T2, T4, T5, T6 = 8, 9, 10, 12, 13, 14
S0, S1, S3 = 16, 17, 19
SP, RA = 29, 31
V0 = 2

def addiu(rt, rs, imm):
    return (0x09 << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)

def sw(rt, base, offset):
    return (0x2B << 26) | (base << 21) | (rt << 16) | (offset & 0xFFFF)

def lw(rt, base, offset):
    return (0x23 << 26) | (base << 21) | (rt << 16) | (offset & 0xFFFF)

def lhu(rt, base, offset):
    return (0x25 << 26) | (base << 21) | (rt << 16) | (offset & 0xFFFF)

def lbu(rt, base, offset):
    return (0x24 << 26) | (base << 21) | (rt << 16) | (offset & 0xFFFF)

def sb(rt, base, offset):
    return (0x28 << 26) | (base << 21) | (rt << 16) | (offset & 0xFFFF)

def lui(rt, imm):
    return (0x0F << 26) | (rt << 16) | (imm & 0xFFFF)

def ori(rt, rs, imm):
    return (0x0D << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)

def jal(target):
    return (0x03 << 26) | ((target >> 2) & 0x03FFFFFF)

def j(target):
    return (0x02 << 26) | ((target >> 2) & 0x03FFFFFF)

def jr(rs):
    return (rs << 21) | 0x08

def beq(rs, rt, offset_words):
    return (0x04 << 26) | (rs << 21) | (rt << 16) | (offset_words & 0xFFFF)

def bne(rs, rt, offset_words):
    return (0x05 << 26) | (rs << 21) | (rt << 16) | (offset_words & 0xFFFF)

def sll(rd, rt, sa):
    return (rt << 16) | (rd << 11) | (sa << 6) | 0x00

def srl(rd, rt, sa):
    return (rt << 16) | (rd << 11) | (sa << 6) | 0x02

def addu(rd, rs, rt):
    return (rs << 21) | (rt << 16) | (rd << 11) | 0x21

def li(rt, imm):
    """Load 16-bit immediate (sign-extended). Use for values 0-0x7FFF."""
    return addiu(rt, ZERO, imm)

def nop():
    return 0x00000000

# --- Instruction list builder ---
class CodeBlock:
    def __init__(self, base_addr):
        self.base = base_addr
        self.instructions = []  # (encoding, comment)

    def add(self, encoding, comment=""):
        self.instructions.append((encoding, comment))

    def addr(self, index=None):
        """Current address (next instruction) or address of given index."""
        if index is not None:
            return self.base + index * 4
        return self.base + len(self.instructions) * 4

    def size(self):
        return len(self.instructions) * 4

    def branch_offset(self, target_index):
        """Calculate branch offset from NEXT instruction to target index."""
        current = len(self.instructions)
        return target_index - current - 1

    def pnach_lines(self):
        lines = []
        for i, (enc, comment) in enumerate(self.instructions):
            addr = self.base + i * 4
            if comment:
                lines.append(f"// {comment}")
            lines.append(f"patch=1,EE,2{addr:07X},extended,{enc:08X}")
        return lines


# ===================================================================
# Display hook at 0x01E00000
# Replaces jal FUN_00268270 at 0x00200844 in main frame loop.
# Checks timer; if > 0, renders 2 lines of text, then calls original.
# ===================================================================
display = CodeBlock(0x01E00000)

# Prologue
display.add(addiu(SP, SP, -0x20),     "addiu sp, sp, -0x20")
display.add(sw(RA, SP, 0x00),         "sw ra, 0x00(sp)")
display.add(sw(S0, SP, 0x04),         "sw s0, 0x04(sp)")

# Load base and timer
display.add(lui(S0, 0x01E0),          "lui s0, 0x01E0  ; s0 = base")
display.add(lw(V0, S0, 0x0500),       "lw v0, 0x0500(s0)  ; timer")

# if timer == 0, skip to call_original
skip_target = 15  # index of "call_original" label (will verify)
display.add(beq(V0, ZERO, display.branch_offset(skip_target)),
            "beq v0, zero, call_original")
display.add(nop(),                     "nop")

# Decrement timer
display.add(addiu(V0, V0, -1),        "addiu v0, v0, -1")
display.add(sw(V0, S0, 0x0500),       "sw v0, 0x0500(s0)")

# Render line 1
display.add(addiu(A0, S0, 0x0600),    "addiu a0, s0, 0x0600  ; line1 buf")
display.add(li(A1, 0x44),             "li a1, 0x44  ; x=68")
display.add(jal(0x00268498),          "jal render_text_string")
display.add(li(A2, 0xC8),             "li a2, 0xC8  ; y=200 [delay]")

# Render line 2
display.add(addiu(A0, S0, 0x0640),    "addiu a0, s0, 0x0640  ; line2 buf")
display.add(li(A1, 0x44),             "li a1, 0x44  ; x=68")
assert len(display.instructions) == skip_target, \
    f"skip_target mismatch: {len(display.instructions)} != {skip_target}"

# call_original label (index 15)
display.add(jal(0x00268270),          "call_original: jal FUN_00268270")
display.add(li(A2, 0xB4),             "li a2, 0xB4  ; y=180 [delay]")

# Wait — I put the render_text_string jal for line 2 BEFORE the call_original label.
# Let me fix this. The call_original should be the jal to FUN_00268270.
# But I'm combining: if timer > 0, render line 2 then call original.
# If timer == 0, skip straight to call original.
# The issue: line 2's jal to render_text_string is at index 14 (a1 load)
# and the skip goes to index 15 (jal FUN_00268270).
# But line 2 also needs a jal to render_text_string!

# Let me restructure:
display = CodeBlock(0x01E00000)

# Prologue
display.add(addiu(SP, SP, -0x20),     "addiu sp, sp, -0x20")
display.add(sw(RA, SP, 0x00),         "sw ra, 0x00(sp)")
display.add(sw(S0, SP, 0x04),         "sw s0, 0x04(sp)")

# Load base and timer
display.add(lui(S0, 0x01E0),          "lui s0, 0x01E0")
display.add(lw(V0, S0, 0x0500),       "lw v0, 0x0500(s0)  ; timer")

# if timer == 0, skip display (branch target = call_original at index 25)
call_orig_idx = 25
display.add(beq(V0, ZERO, display.branch_offset(call_orig_idx)),
            "beq v0, zero, call_original")
display.add(nop(),                     "nop")

# Decrement timer
display.add(addiu(V0, V0, -1),        "addiu v0, v0, -1")
display.add(sw(V0, S0, 0x0500),       "sw v0, 0x0500(s0)  ; store timer")

# Render line 1
display.add(addiu(A0, S0, 0x0600),    "addiu a0, s0, 0x0600  ; &line1")
display.add(li(A1, 0x44),             "li a1, 0x44  ; x=68")
display.add(jal(0x00268498),          "jal render_text_string")
display.add(li(A2, 0xC8),             "li a2, 0xC8  ; y=200 [delay]")

# Render line 2
display.add(addiu(A0, S0, 0x0640),    "addiu a0, s0, 0x0640  ; &line2")
display.add(li(A1, 0x44),             "li a1, 0x44  ; x=68")
display.add(jal(0x00268498),          "jal render_text_string")
display.add(li(A2, 0xB6),             "li a2, 0xB6  ; y=182 [delay]")

# Render line 3 (effectiveness P/L/W)
display.add(addiu(A0, S0, 0x0680),    "addiu a0, s0, 0x0680  ; &line3")
display.add(li(A1, 0x44),             "li a1, 0x44  ; x=68")
display.add(jal(0x00268498),          "jal render_text_string")
display.add(li(A2, 0xA4),             "li a2, 0xA4  ; y=164 [delay]")

# Render line 4 (effectiveness F/D/I)
display.add(addiu(A0, S0, 0x06C0),    "addiu a0, s0, 0x06C0  ; &line4")
display.add(li(A1, 0x44),             "li a1, 0x44  ; x=68")
display.add(jal(0x00268498),          "jal render_text_string")
display.add(li(A2, 0x92),             "li a2, 0x92  ; y=146 [delay]")

# call_original (index 25)
assert len(display.instructions) == call_orig_idx, \
    f"call_orig index mismatch: {len(display.instructions)} != {call_orig_idx}"
display.add(jal(0x00268270),          "call_original: jal FUN_00268270")
display.add(nop(),                     "nop  [delay]")

# Epilogue
display.add(lw(S0, SP, 0x04),         "lw s0, 0x04(sp)")
display.add(lw(RA, SP, 0x00),         "lw ra, 0x00(sp)")
display.add(jr(RA),                    "jr ra")
display.add(addiu(SP, SP, 0x20),      "addiu sp, sp, 0x20  [delay]")

print(f"Display hook: {display.size()} bytes ({len(display.instructions)} insns) "
      f"at 0x{display.base:08X}-0x{display.addr()-1:08X}")


# ===================================================================
# hex4 subroutine at 0x01E00100
# Converts 16-bit value to 4 ASCII hex chars.
# Input: t0 = value (lower 16 bits), t1 = output ptr, t5 = hex table
# Output: t1 advanced by 4
# Clobbers: t0, t2, t4
# ===================================================================
hex4 = CodeBlock(0x01E00100)

hex4.add(sll(T0, T0, 16),             "sll t0, t0, 16  ; value to upper 16")
hex4.add(li(T4, 4),                   "li t4, 4  ; nibble counter")
# loop (index 2)
loop_idx = 2
hex4.add(srl(T2, T0, 28),             "loop: srl t2, t0, 28  ; top nibble")
hex4.add(addu(T2, T2, T5),            "addu t2, t2, t5  ; index hex table")
hex4.add(lbu(T2, T2, 0),              "lbu t2, 0(t2)  ; hex char")
hex4.add(sb(T2, T1, 0),               "sb t2, 0(t1)  ; store")
hex4.add(addiu(T1, T1, 1),            "addiu t1, t1, 1")
hex4.add(sll(T0, T0, 4),              "sll t0, t0, 4  ; next nibble")
hex4.add(addiu(T4, T4, -1),           "addiu t4, t4, -1")
hex4.add(bne(T4, ZERO, hex4.branch_offset(loop_idx)),
         "bne t4, zero, loop")
hex4.add(nop(),                        "nop  [delay]")
hex4.add(jr(RA),                       "jr ra")
hex4.add(nop(),                        "nop  [delay]")

print(f"hex4 subroutine: {hex4.size()} bytes ({len(hex4.instructions)} insns) "
      f"at 0x{hex4.base:08X}-0x{hex4.addr()-1:08X}")


# ===================================================================
# hex2 subroutine at 0x01E00140
# Converts 8-bit value to 2 ASCII hex chars.
# Input: t0 = value (lower 8 bits), t1 = output ptr, t5 = hex table
# Output: t1 advanced by 2
# Shares hex4's loop at 0x01E00108.
# ===================================================================
HEX2_ADDR = 0x01E00140
hex2 = CodeBlock(HEX2_ADDR)
hex2.add(sll(T0, T0, 24),             "sll t0, t0, 24  ; value to upper 8 bits")
hex2.add(li(T4, 2),                   "li t4, 2  ; 2 nibbles")
hex2.add(j(0x01E00108),               "j hex4_loop  ; shared loop")
hex2.add(nop(),                        "nop  [delay]")

print(f"hex2 subroutine: {hex2.size()} bytes ({len(hex2.instructions)} insns) "
      f"at 0x{hex2.base:08X}-0x{hex2.addr()-1:08X}")


# ===================================================================
# Hex lookup table at 0x01E00180
# ===================================================================
HEX_TABLE_ADDR = 0x01E00180
hex_table_bytes = b"0123456789ABCDEF"
hex_table_words = []
for i in range(0, 16, 4):
    w = struct.unpack('<I', hex_table_bytes[i:i+4])[0]
    hex_table_words.append(w)


# ===================================================================
# Damage capture hook at 0x01E00200
# Replaces jal FUN_002d5630 at 0x002163f8 inside FUN_00216140.
# Delay slot at 0x002163fc (sltu a0,zero,a0) still executes.
#
# On entry:
#   a0 = (entity_flags & 0x48) != 0  (is_enemy, computed by delay slot)
#   a1 = max HP         (lh from target+0x12A)
#   a2 = current HP     (lh from target+0x128)
#   a3 = pending damage  (lh from target+0xBE, = this hit's damage)
#   s0 = target entity ptr  (from FUN_00216140)
#   s1 = attack data ptr    (from FUN_00216140)
#   s3 = attacker entity ptr (from FUN_00216140)
#
# Builds display strings and calls original FUN_002d5630.
# ===================================================================
HEX4_ADDR = 0x01E00100

dmg = CodeBlock(0x01E00200)

# --- Prologue: save ra and args ---
dmg.add(addiu(SP, SP, -0x30),        "addiu sp, sp, -0x30")
dmg.add(sw(RA, SP, 0x00),            "sw ra, 0x00(sp)")
dmg.add(sw(A0, SP, 0x04),            "sw a0, 0x04(sp)  ; is_enemy")
dmg.add(sw(A1, SP, 0x08),            "sw a1, 0x08(sp)  ; max_hp")
dmg.add(sw(A2, SP, 0x0C),            "sw a2, 0x0C(sp)  ; cur_hp")
dmg.add(sw(A3, SP, 0x10),            "sw a3, 0x10(sp)  ; damage")

# --- Setup base pointers ---
dmg.add(lui(T6, 0x01E0),             "lui t6, 0x01E0  ; base")
dmg.add(li(T0, 300),                 "li t0, 300  ; timer ~5s @60fps")
dmg.add(sw(T0, T6, 0x0500),          "sw t0, 0x0500(t6)  ; set timer")
dmg.add(addiu(T5, T6, 0x0180),       "addiu t5, t6, 0x0180  ; hex table")

# ======== Build Line 1: "E:XXXX T:XXXX >XXXX" ========
dmg.add(addiu(T1, T6, 0x0600),       "addiu t1, t6, 0x0600  ; line1 buf")

# "E:" (0x45='E', 0x3A=':')
dmg.add(li(T0, 0x3A45),              "li t0, 0x3A45  ; 'E:'")
# sh stores: byte[0]=0x45('E'), byte[1]=0x3A(':') on little-endian
dmg.add((0x29 << 26) | (T1 << 21) | (T0 << 16) | 0,  "sh t0, 0(t1)")  # sh t0, 0(t1)
dmg.add(addiu(T1, T1, 2),            "addiu t1, t1, 2")

# hex4(attack element = s1[0])
dmg.add(lhu(T0, S1, 0x00),           "lhu t0, 0x00(s1)  ; atk element")
dmg.add(jal(HEX4_ADDR),              "jal hex4")
dmg.add(nop(),                        "nop")

# " T:" (0x20=' ', 0x54='T', 0x3A=':')
dmg.add(li(T0, 0x5420),              "li t0, 0x5420  ; ' T'")
dmg.add((0x29 << 26) | (T1 << 21) | (T0 << 16) | 0,  "sh t0, 0(t1)")
dmg.add(li(T0, 0x3A),                "li t0, 0x3A  ; ':'")
dmg.add(sb(T0, T1, 2),               "sb t0, 2(t1)")
dmg.add(addiu(T1, T1, 3),            "addiu t1, t1, 3")

# hex4(damage type = s1+3, byte, zero-extended)
dmg.add(lbu(T0, S1, 0x03),           "lbu t0, 0x03(s1)  ; damage type")
dmg.add(jal(HEX4_ADDR),              "jal hex4")
dmg.add(nop(),                        "nop")

# " >" using byte stores (t1 may be at odd offset here)
dmg.add(li(T0, 0x20),                "li t0, 0x20  ; ' '")
dmg.add(sb(T0, T1, 0),               "sb t0, 0(t1)")
dmg.add(li(T0, 0x3E),                "li t0, 0x3E  ; '>'")
dmg.add(sb(T0, T1, 1),               "sb t0, 1(t1)")
dmg.add(addiu(T1, T1, 2),            "addiu t1, t1, 2")

# hex4(pending damage = a3, from stack)
dmg.add(lw(T0, SP, 0x10),            "lw t0, 0x10(sp)  ; damage (a3)")
dmg.add(jal(HEX4_ADDR),              "jal hex4")
dmg.add(nop(),                        "nop")

# null terminate line 1
dmg.add(sb(ZERO, T1, 0),             "sb zero, 0(t1)  ; null term")

# ======== Build Line 2: "Df:XXXX HP:XXXX/XXXX" ========
dmg.add(addiu(T1, T6, 0x0640),       "addiu t1, t6, 0x0640  ; line2 buf")

# "Df:" (0x44='D', 0x66='f', 0x3A=':')
dmg.add(li(T0, 0x6644),              "li t0, 0x6644  ; 'Df'")
dmg.add((0x29 << 26) | (T1 << 21) | (T0 << 16) | 0,  "sh t0, 0(t1)")
dmg.add(li(T0, 0x3A),                "li t0, 0x3A  ; ':'")
dmg.add(sb(T0, T1, 2),               "sb t0, 2(t1)")
dmg.add(addiu(T1, T1, 3),            "addiu t1, t1, 3")

# hex4(defense = target+0x12E)
dmg.add(lhu(T0, S0, 0x012E),         "lhu t0, 0x012E(s0)  ; defense")
dmg.add(jal(HEX4_ADDR),              "jal hex4")
dmg.add(nop(),                        "nop")

# " HP:" (0x20=' ', 0x48='H', 0x50='P', 0x3A=':')
# As a word (little-endian): byte0=' ', byte1='H', byte2='P', byte3=':'
# = 0x3A504820
dmg.add(lui(T0, 0x3A50),             "lui t0, 0x3A50")
dmg.add(ori(T0, T0, 0x4820),         "ori t0, t0, 0x4820  ; ' HP:'")
dmg.add(sw(T0, T1, 0),               "sw t0, 0(t1)")  # sw = 0x2B
# Fix: sw is store word. Let me use the raw encoding for sw:
# Actually sw is already defined above. Let me check alignment.
# t1 points after hex4 output (which wrote 4 bytes via sb). The starting
# position of line 2 is 0x01E00640 (word-aligned). After "Df:" (3 bytes),
# t1 = 0x01E00643. After hex4 (4 bytes via sb), t1 = 0x01E00647.
# Storing a word at 0x01E00647 would be UNALIGNED! MIPS requires word
# stores to be 4-byte aligned. This will cause an exception.
# I need to use byte stores for " HP:".

# Let me redo this. Actually wait, the PS2's EE (R5900) might handle
# unaligned stores differently, but to be safe, let me use byte stores.

# Scratch that sw approach, redo with byte stores.
dmg.instructions = dmg.instructions[:-3]  # remove the last 3 (lui, ori, sw)

# " HP:" using byte stores
dmg.add(li(T0, 0x20),                "li t0, 0x20  ; ' '")
dmg.add(sb(T0, T1, 0),               "sb t0, 0(t1)")
dmg.add(li(T0, 0x48),                "li t0, 0x48  ; 'H'")
dmg.add(sb(T0, T1, 1),               "sb t0, 1(t1)")
dmg.add(li(T0, 0x50),                "li t0, 0x50  ; 'P'")
dmg.add(sb(T0, T1, 2),               "sb t0, 2(t1)")
dmg.add(li(T0, 0x3A),                "li t0, 0x3A  ; ':'")
dmg.add(sb(T0, T1, 3),               "sb t0, 3(t1)")
dmg.add(addiu(T1, T1, 4),            "addiu t1, t1, 4")

# hex4(current HP = a2, from stack)
dmg.add(lw(T0, SP, 0x0C),            "lw t0, 0x0C(sp)  ; cur HP (a2)")
dmg.add(jal(HEX4_ADDR),              "jal hex4")
dmg.add(nop(),                        "nop")

# "/" (0x2F)
dmg.add(li(T0, 0x2F),                "li t0, 0x2F  ; '/'")
dmg.add(sb(T0, T1, 0),               "sb t0, 0(t1)")
dmg.add(addiu(T1, T1, 1),            "addiu t1, t1, 1")

# hex4(max HP = a1, from stack)
dmg.add(lw(T0, SP, 0x08),            "lw t0, 0x08(sp)  ; max HP (a1)")
dmg.add(jal(HEX4_ADDR),              "jal hex4")
dmg.add(nop(),                        "nop")

# null terminate line 2
dmg.add(sb(ZERO, T1, 0),             "sb zero, 0(t1)  ; null term")

# ======== Build Line 3: "P:XX L:XX W:XX" ========
# Effectiveness table is on FUN_00216140's stack at sp+0x868 (from disasm:
# lb a1,0x868(v0)). Our hook's sp is 0x30 below caller's, so table is
# at our_sp + 0x898. Element byte indices: 0=Phys, 1=Ltn, 2=Wind,
# 4=Fire, 5=Dark, 10=Ice.
EFF_BASE = 0x898  # sp offset to effectiveness table from our frame

dmg.add(addiu(T1, T6, 0x0680),       "addiu t1, t6, 0x0680  ; line3 buf")

# "P:" (0x50='P', 0x3A=':')
dmg.add(li(T0, 0x3A50),              "li t0, 0x3A50  ; 'P:'")
dmg.add((0x29 << 26) | (T1 << 21) | (T0 << 16) | 0,  "sh t0, 0(t1)")
dmg.add(addiu(T1, T1, 2),            "addiu t1, t1, 2")

# hex2(Physical = eff[0])
dmg.add(lbu(T0, SP, EFF_BASE + 0),   "lbu t0, 0x%X(sp)  ; eff[0] Physical" % (EFF_BASE + 0))
dmg.add(jal(HEX2_ADDR),              "jal hex2")
dmg.add(nop(),                        "nop")

# " L:" (0x20=' ', 0x4C='L', 0x3A=':')
dmg.add(li(T0, 0x20),                "li t0, 0x20  ; ' '")
dmg.add(sb(T0, T1, 0),               "sb t0, 0(t1)")
dmg.add(li(T0, 0x4C),                "li t0, 0x4C  ; 'L'")
dmg.add(sb(T0, T1, 1),               "sb t0, 1(t1)")
dmg.add(li(T0, 0x3A),                "li t0, 0x3A  ; ':'")
dmg.add(sb(T0, T1, 2),               "sb t0, 2(t1)")
dmg.add(addiu(T1, T1, 3),            "addiu t1, t1, 3")

# hex2(Lightning = eff[1])
dmg.add(lbu(T0, SP, EFF_BASE + 1),   "lbu t0, 0x%X(sp)  ; eff[1] Lightning" % (EFF_BASE + 1))
dmg.add(jal(HEX2_ADDR),              "jal hex2")
dmg.add(nop(),                        "nop")

# " W:" (0x20=' ', 0x57='W', 0x3A=':')
dmg.add(li(T0, 0x20),                "li t0, 0x20  ; ' '")
dmg.add(sb(T0, T1, 0),               "sb t0, 0(t1)")
dmg.add(li(T0, 0x57),                "li t0, 0x57  ; 'W'")
dmg.add(sb(T0, T1, 1),               "sb t0, 1(t1)")
dmg.add(li(T0, 0x3A),                "li t0, 0x3A  ; ':'")
dmg.add(sb(T0, T1, 2),               "sb t0, 2(t1)")
dmg.add(addiu(T1, T1, 3),            "addiu t1, t1, 3")

# hex2(Wind = eff[2])
dmg.add(lbu(T0, SP, EFF_BASE + 2),   "lbu t0, 0x%X(sp)  ; eff[2] Wind" % (EFF_BASE + 2))
dmg.add(jal(HEX2_ADDR),              "jal hex2")
dmg.add(nop(),                        "nop")

# null terminate line 3
dmg.add(sb(ZERO, T1, 0),             "sb zero, 0(t1)  ; null term line3")

# ======== Build Line 4: "F:XX D:XX I:XX" ========
dmg.add(addiu(T1, T6, 0x06C0),       "addiu t1, t6, 0x06C0  ; line4 buf")

# "F:" (0x46='F', 0x3A=':')
dmg.add(li(T0, 0x3A46),              "li t0, 0x3A46  ; 'F:'")
dmg.add((0x29 << 26) | (T1 << 21) | (T0 << 16) | 0,  "sh t0, 0(t1)")
dmg.add(addiu(T1, T1, 2),            "addiu t1, t1, 2")

# hex2(Fire = eff[4])
dmg.add(lbu(T0, SP, EFF_BASE + 4),   "lbu t0, 0x%X(sp)  ; eff[4] Fire" % (EFF_BASE + 4))
dmg.add(jal(HEX2_ADDR),              "jal hex2")
dmg.add(nop(),                        "nop")

# " D:" (0x20=' ', 0x44='D', 0x3A=':')
dmg.add(li(T0, 0x20),                "li t0, 0x20  ; ' '")
dmg.add(sb(T0, T1, 0),               "sb t0, 0(t1)")
dmg.add(li(T0, 0x44),                "li t0, 0x44  ; 'D'")
dmg.add(sb(T0, T1, 1),               "sb t0, 1(t1)")
dmg.add(li(T0, 0x3A),                "li t0, 0x3A  ; ':'")
dmg.add(sb(T0, T1, 2),               "sb t0, 2(t1)")
dmg.add(addiu(T1, T1, 3),            "addiu t1, t1, 3")

# hex2(Dark = eff[5])
dmg.add(lbu(T0, SP, EFF_BASE + 5),   "lbu t0, 0x%X(sp)  ; eff[5] Dark" % (EFF_BASE + 5))
dmg.add(jal(HEX2_ADDR),              "jal hex2")
dmg.add(nop(),                        "nop")

# " I:" (0x20=' ', 0x49='I', 0x3A=':')
dmg.add(li(T0, 0x20),                "li t0, 0x20  ; ' '")
dmg.add(sb(T0, T1, 0),               "sb t0, 0(t1)")
dmg.add(li(T0, 0x49),                "li t0, 0x49  ; 'I'")
dmg.add(sb(T0, T1, 1),               "sb t0, 1(t1)")
dmg.add(li(T0, 0x3A),                "li t0, 0x3A  ; ':'")
dmg.add(sb(T0, T1, 2),               "sb t0, 2(t1)")
dmg.add(addiu(T1, T1, 3),            "addiu t1, t1, 3")

# hex2(Ice = eff[10])
dmg.add(lbu(T0, SP, EFF_BASE + 10),  "lbu t0, 0x%X(sp)  ; eff[10] Ice" % (EFF_BASE + 10))
dmg.add(jal(HEX2_ADDR),              "jal hex2")
dmg.add(nop(),                        "nop")

# null terminate line 4
dmg.add(sb(ZERO, T1, 0),             "sb zero, 0(t1)  ; null term line4")

# --- Epilogue: restore args and call original FUN_002d5630 ---
dmg.add(lw(A0, SP, 0x04),            "lw a0, 0x04(sp)  ; restore args")
dmg.add(lw(A1, SP, 0x08),            "lw a1, 0x08(sp)")
dmg.add(lw(A2, SP, 0x0C),            "lw a2, 0x0C(sp)")
dmg.add(lw(A3, SP, 0x10),            "lw a3, 0x10(sp)")
dmg.add(jal(0x002d5630),             "jal FUN_002d5630  ; original")
dmg.add(nop(),                        "nop")

dmg.add(lw(RA, SP, 0x00),            "lw ra, 0x00(sp)")
dmg.add(jr(RA),                       "jr ra")
dmg.add(addiu(SP, SP, 0x30),         "addiu sp, sp, 0x30  [delay]")

print(f"Damage hook: {dmg.size()} bytes ({len(dmg.instructions)} insns) "
      f"at 0x{dmg.base:08X}-0x{dmg.addr()-1:08X}")

# ===================================================================
# Verify all jump targets
# ===================================================================
def verify_jal(name, from_addr, encoding, expected_target):
    target = (encoding & 0x03FFFFFF) << 2
    ok = "OK" if target == expected_target else "MISMATCH!"
    print(f"  {name}: jal from 0x{from_addr:08X} -> 0x{target:08X} "
          f"(expect 0x{expected_target:08X}) [{ok}]")
    assert target == expected_target, f"jal target mismatch for {name}"

print("\nVerifying jump targets:")

# Display hook calls
for i, (enc, cmt) in enumerate(display.instructions):
    addr = display.base + i * 4
    if (enc >> 26) == 0x03:  # jal
        target = (enc & 0x03FFFFFF) << 2
        verify_jal(cmt, addr, enc, target)

# Damage hook calls
for i, (enc, cmt) in enumerate(dmg.instructions):
    addr = dmg.base + i * 4
    if (enc >> 26) == 0x03:  # jal
        target = (enc & 0x03FFFFFF) << 2
        verify_jal(cmt, addr, enc, target)

# hex2 jump target
for i, (enc, cmt) in enumerate(hex2.instructions):
    addr = hex2.base + i * 4
    if (enc >> 26) == 0x02:  # j
        target = (enc & 0x03FFFFFF) << 2
        ok = "OK" if target == 0x01E00108 else "MISMATCH!"
        print(f"  {cmt}: j from 0x{addr:08X} -> 0x{target:08X} "
              f"(expect 0x01E00108) [{ok}]")
        assert target == 0x01E00108

# Verify branch targets
print("\nVerifying branch targets:")
for i, (enc, cmt) in enumerate(display.instructions):
    if (enc >> 26) == 0x04:  # beq
        offset = enc & 0xFFFF
        if offset & 0x8000:
            offset -= 0x10000
        target_addr = display.base + (i + 1) * 4 + offset * 4
        print(f"  beq at 0x{display.base + i*4:08X} -> 0x{target_addr:08X} ({cmt})")

for i, (enc, cmt) in enumerate(hex4.instructions):
    if (enc >> 26) == 0x05:  # bne
        offset = enc & 0xFFFF
        if offset & 0x8000:
            offset -= 0x10000
        target_addr = hex4.base + (i + 1) * 4 + offset * 4
        print(f"  bne at 0x{hex4.base + i*4:08X} -> 0x{target_addr:08X} ({cmt})")


# ===================================================================
# Generate pnach output
# ===================================================================
print("\n" + "=" * 70)
print("PNACH OUTPUT")
print("=" * 70)

lines = []
lines.append("// Battle Damage HUD patch - displays attack/target info on hit")
lines.append("//")
lines.append("// When an attack triggers the HP bar update (FUN_002d5630), captures:")
lines.append("//   Attack element flags, damage type, net damage dealt")
lines.append("//   Target defense stat, current HP, max HP")
lines.append("//   Target elemental effectiveness table (6 elements)")
lines.append("// Displays as 4 lines of hex in the upper-right for ~5 seconds.")
lines.append("//")
lines.append("// Display format:")
lines.append('//   Line 1: "E:XXXX T:XXXX >XXXX"  (attack element, dmg type, damage)')
lines.append('//   Line 2: "Df:XXXX HP:XXXX/XXXX" (defense, HP current/max)')
lines.append('//   Line 3: "P:XX L:XX W:XX"       (physical, lightning, wind effectiveness)')
lines.append('//   Line 4: "F:XX D:XX I:XX"       (fire, dark, ice effectiveness)')
lines.append("//")
lines.append("// Hooks:")
lines.append("//   0x00200844: jal FUN_00268270 -> jal display_hook (per-frame)")
lines.append("//   0x002163f8: jal FUN_002d5630 -> jal capture_hook (on damage)")
lines.append("//   0x002163d8: beq -> nop (remove entity type flag gate for bosses)")
lines.append("//   0x002163e8: bne -> nop (remove entity status flag gate for bosses)")
lines.append("//")
lines.append("// Code cave: 0x01E00000+ (unused EE RAM, ~701KB above .bss,")
lines.append("//   ~2MB below $sp @ 0x01FFFCC0)")
lines.append("//")
lines.append("// Data captured from inside FUN_00216140 (damage calculation):")
lines.append("//   s0 = target entity, s1 = attack data, s3 = attacker")
lines.append("//   a1 = max HP, a2 = current HP, a3 = pending damage")
lines.append("//   s1[0] = attack element flags, s1+3 = damage type byte")
lines.append("//   s0+0x12E = target defense stat")
lines.append("//   Effectiveness table at caller sp+0x868 (acStack_88, 16 bytes)")
lines.append("//")
lines.append("// Compatible with boss_pentagon.pnach (separate hooks and memory).")
lines.append("//   boss_pentagon hooks 0x00240838 and uses 0x312920.")
lines.append("//   This patch hooks 0x00200844 and 0x002163f8, uses 0x01E00000+.")
lines.append("")

# Hook 1: display hook
hook1 = jal(0x01E00000)
lines.append("// " + "-" * 60)
lines.append("// Hook 1: per-frame display (replace jal FUN_00268270 in main loop)")
lines.append(f"// Original: {jal(0x00268270):08X} (jal 0x00268270)")
lines.append("// " + "-" * 60)
lines.append(f"patch=1,EE,20200844,extended,{hook1:08X}")
lines.append("")

# Hook 2: damage capture
hook2 = jal(0x01E00200)
lines.append("// " + "-" * 60)
lines.append("// Hook 2: damage capture (replace jal FUN_002d5630 in FUN_00216140)")
lines.append(f"// Original: {jal(0x002d5630):08X} (jal 0x002d5630)")
lines.append("// Delay slot at 0x002163fc (sltu a0,zero,a0) still executes.")
lines.append("// " + "-" * 60)
lines.append(f"patch=1,EE,202163F8,extended,{hook2:08X}")
lines.append("")

# Guard bypass NOPs: remove entity flag checks that gate the FUN_002d5630 call.
# Bosses don't pass these checks, so the capture hook never fires for them.
# NOP both branches to allow our hook to fire for all entity types.
lines.append("// " + "-" * 60)
lines.append("// Boss fix: NOP entity flag checks that gate the FUN_002d5630 call")
lines.append("// Without these, bosses skip the call (and our capture hook).")
lines.append("// The damage > 0 check at 0x002163c8 (blez) is preserved.")
lines.append("// " + "-" * 60)
lines.append("// NOP entity type flag check: beq v0,zero,0x216404 if (flags & 0x4B)==0")
lines.append("// Original: 1040000A (beq v0, zero, +0x0A)")
lines.append("patch=1,EE,202163D8,extended,00000000")
lines.append("// NOP entity status flag check: bne v0,zero,0x216404 if (status & 0x20)")
lines.append("// Original: 14400006 (bne v0, zero, +0x06)")
lines.append("patch=1,EE,202163E8,extended,00000000")
lines.append("")

# Display hook code
lines.append("// " + "-" * 60)
lines.append("// Display hook at 0x01E00000")
lines.append("// " + "-" * 60)
for pline in display.pnach_lines():
    lines.append(pline)
lines.append("")

# hex4 subroutine
lines.append("// " + "-" * 60)
lines.append("// hex4 subroutine at 0x01E00100")
lines.append("// t0=value(16-bit), t1=output ptr, t5=hex table base")
lines.append("// " + "-" * 60)
for pline in hex4.pnach_lines():
    lines.append(pline)
lines.append("")

# hex2 subroutine
lines.append("// " + "-" * 60)
lines.append("// hex2 subroutine at 0x01E00140")
lines.append("// t0=value(8-bit), t1=output ptr, t5=hex table base")
lines.append("// Shares hex4's loop at 0x01E00108")
lines.append("// " + "-" * 60)
for pline in hex2.pnach_lines():
    lines.append(pline)
lines.append("")

# Hex lookup table
lines.append("// " + "-" * 60)
lines.append('// Hex lookup table "0123456789ABCDEF" at 0x01E00180')
lines.append("// " + "-" * 60)
for i, w in enumerate(hex_table_words):
    addr = HEX_TABLE_ADDR + i * 4
    chars = hex_table_bytes[i*4:(i+1)*4].decode('ascii')
    lines.append(f'// "{chars}"')
    lines.append(f'patch=1,EE,2{addr:07X},extended,{w:08X}')
lines.append("")

# Damage capture hook code
lines.append("// " + "-" * 60)
lines.append("// Damage capture hook at 0x01E00200")
lines.append("// " + "-" * 60)
for pline in dmg.pnach_lines():
    lines.append(pline)

# Print to stdout
pnach_content = "\n".join(lines) + "\n"
print(pnach_content)

# Also write to file
with open("battle_damage_hud.pnach", "w") as f:
    f.write(pnach_content)

print(f"\nWritten to battle_damage_hud.pnach")
print(f"Total patch lines: {sum(1 for l in lines if l.startswith('patch='))}")
