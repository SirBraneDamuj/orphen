// Analyzed re-expression of FUN_0025d0e0
// Original signature: void FUN_0025d0e0(undefined4 param_1, char param_2)
//
// Purpose (inferred):
// - Populate a small command/control block at DAT_00355724 with a fixed screen-space
//   rectangle (±320 x ±224 in float) and four copies of a 32-bit word, then kick a
//   lower-level dispatcher (FUN_00207de8) with code 0x1007.
// - The mode flag selects one of two constants written at offset +0x0C (0x40180 vs 0x44180).
//
// Details:
// - Writes the following floats (IEEE-754):
//     -224.0 at +0x34 and +0x44
//     -320.0 at +0x20 and +0x30
//     +320.0 at +0x40 and +0x50
//     +224.0 at +0x24 and +0x54
//   This defines a rectangle spanning [-320, +320] x [-224, +224].
// - Takes a 32-bit payload_word and duplicates it into four consecutive dwords at
//   +0x10,+0x14,+0x18,+0x1C.
// - Writes a global constant DAT_00352b68 into four slots at +0x28,+0x38,+0x48,+0x58
//   (stride of 0x10 per iteration).
// - Sets 16-bit fields: *(+0x04)=4, *(+0x06)=0xFFFF.
// - Sets *(+0x0C)=0x40180, or 0x44180 if use_alt_mode != 0.
// - Calls FUN_00207de8(0x1007).
//
// Notes:
// - Callers typically feed payload_word as either 0 or with 0xFF000000 tag applied by callers
//   (see FUN_0025d1c0 analysis).
// - The exact hardware meaning of 0x40180/0x44180 and 0x1007 is TBD; this likely interacts
//   with a render or DMA subsystem on the PS2 EE/GS/VIF path.

#include <stdint.h>

extern int DAT_00355724;                 // base of control/packet structure
extern uint32_t DAT_00352b68;            // global command word used in the packet
extern void FUN_00207de8(uint32_t code); // lower-level dispatcher/kick

void build_and_submit_view_rect_packet(uint32_t payload_word, int use_alt_mode)
{
  int base = DAT_00355724;

  // Rectangle (floats)
  *(uint32_t *)(base + 0x34) = 0xC3600000; // -224.0f
  *(uint32_t *)(base + 0x44) = 0xC3600000; // -224.0f
  *(uint32_t *)(base + 0x20) = 0xC3A00000; // -320.0f
  *(uint32_t *)(base + 0x40) = 0x43A00000; // +320.0f
  *(uint32_t *)(base + 0x24) = 0x43600000; // +224.0f
  *(uint32_t *)(base + 0x30) = 0xC3A00000; // -320.0f
  *(uint32_t *)(base + 0x50) = 0x43A00000; // +320.0f
  *(uint32_t *)(base + 0x54) = 0x43600000; // +224.0f

  // Duplicate global command and payload_word across four slots
  uint32_t cmd = DAT_00352b68;
  uint32_t *p_cmd = (uint32_t *)(base + 0x28);
  uint32_t *p_pay = (uint32_t *)(base + 0x10);
  for (int i = 0; i < 4; ++i)
  {
    p_cmd[i * 4] = cmd;      // +0x28, +0x38, +0x48, +0x58
    p_pay[i] = payload_word; // +0x10, +0x14, +0x18, +0x1C
  }

  // Header fields
  *(uint16_t *)(base + 0x04) = 4;
  *(uint16_t *)(base + 0x06) = 0xFFFF;
  *(uint32_t *)(base + 0x0C) = use_alt_mode ? 0x44180 : 0x40180;

  // Kick
  FUN_00207de8(0x1007);
}
