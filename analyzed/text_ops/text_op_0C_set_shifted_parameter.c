// Text opcode 0x0C — LAB_00239428
// Raw instruction sequence:
//   v1 = *(gp-0x5140)
//   a0 = v1 + 1
//   a1 = v1 + 2
//   *(gp-0x5140) = a0          (advance cursor by 1)
//   v0 = *(uint8_t *)(v1 + 1)  (second byte from original base)
//   *(gp-0x5140) = a1          (advance cursor again; net +2 from original base)
//   v0 <<= 5                   (shift left by 5 bits)
//   *(gp-0x4304) = v0          (store 32x-scaled value)
//   return
//
// Behavior:
//   Consumes two bytes from the dialogue control stream pointer at gp-0x5140. The first consumed byte
//   (original *(v1)) is ignored. The second consumed byte (original *(v1+1)) is loaded, left-shifted by 5,
//   and written to a gp-relative global at gp-0x4304. Cursor advances by exactly 2 bytes total.
//
// Notes:
//   - Output value is always a multiple of 32 (byte << 5). Downstream usage likely treats this as a scaled
//     quantity (e.g., fixed-point magnitude or timing slice). Pending identification of gp-0x4304 usage.
//   - No glyph slot allocation; purely parameter update.
//
// Side effects:
//   - Advances dialogue cursor (gp-0x5140) by 2.
//   - Writes one 32-bit value to gp-0x4304.
//
// TODO:
//   - Map gp-0x4304 to a named global via globals.json and cross-references once located.
//   - Confirm whether the first (ignored) byte is a padding or reserved field in stream format.
//
// Original label preserved in comments for cross-reference.

#include <stdint.h>

extern int GP_NEG_0x5140;          // dialogue control cursor (pending confirmed name)
extern unsigned int GP_NEG_0x4304; // shifted parameter storage (pending name)

void text_op_0C_set_shifted_parameter(void)
{
  int base = GP_NEG_0x5140;
  // Advance by first unused byte
  GP_NEG_0x5140 = base + 1;
  // Read second byte
  unsigned int raw = *(unsigned char *)(base + 1);
  // Advance cursor final (+2 total)
  GP_NEG_0x5140 = base + 2;
  // Store shifted value
  GP_NEG_0x4304 = raw << 5;
}
