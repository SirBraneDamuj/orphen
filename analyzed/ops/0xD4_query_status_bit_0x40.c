// Opcode 0xD4 — query_status_bit_0x40 (analyzed)
// Original: LAB_00264998 (table entry in PTR_LAB_0031e228)
//
// Ghidra snippet:
//   lhu v0, -0x4976(gp)   ; load u16 at GP-0x4976 => DAT_003555fa
//   jr  ra
//   andi v0, v0, 0x40     ; return v0 & 0x40
//
// Summary:
// - Returns the masked value of global DAT_003555fa & 0x40.
// - No side effects.
//
// Notes:
// - DAT_003555fa (u16) is referenced across systems; bit 0x40 is commonly checked elsewhere too.
// - This opcode is a quick status probe usable as a boolean (non-zero if set).

#include <stdint.h>

extern unsigned short DAT_003555fa; // u16 global at 0x003555fa

unsigned int opcode_0xD4_query_status_bit_0x40(void)
{
  return (unsigned int)(DAT_003555fa & 0x0040);
}
