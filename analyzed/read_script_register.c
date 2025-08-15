// read_script_register — analyzed version of FUN_0025c548
// Original: unsigned long FUN_0025c548(unsigned long id);
// Purpose: Dispatches reads from the current object/frame "register bank" based on a small id.
// Notes:
//  - Mirrors the raw decompiled switch in src/FUN_0025c548.c but with descriptive names and comments.
//  - Returns 64-bit unsigned for uniformity (engine often treats register values as scalar ints).
//  - Float cases are scaled by constants (DAT_00352adc..DAT_00352b18) and quantized via FUN_0030bd20.
//  - Some ids return indices derived from pointers by dividing (ptr - DAT_0058beb0) by 0xEC (frame stride).
//  - If a pointer field is null in those cases, returns -1.
//  - Keep original FUN_* name in this header for traceability.
//
// Globals referenced (see globals.json for addresses and xrefs):
//  - DAT_00355044: pointer to current object/frame (base for this register bank)
//  - DAT_0058beb0: base of object/frame pool (stride 0xEC)
//  - DAT_00352adc..DAT_00352b18: float scale factors used for normalization
//
// PS2-specific considerations:
//  - The original uses a multiplicative inverse to divide by 0xEC; we implement direct integer division for clarity.
//  - FUN_0030bd20 likely converts a float to an integer with engine-specific rounding/clamping.

#include <stdint.h>

// Current object/frame pointer (points into a mixed layout; base treated as 16-bit cells)
extern uint16_t *DAT_00355044;

// Base of frame pool used to convert pointers back to indices (stride 0xEC)
extern unsigned char DAT_0058beb0[];

// Float scaling constants
extern float DAT_00352adc, DAT_00352ae0, DAT_00352ae4, DAT_00352ae8, DAT_00352aec,
    DAT_00352af0, DAT_00352af4, DAT_00352af8, DAT_00352afc, DAT_00352b00,
    DAT_00352b04, DAT_00352b08, DAT_00352b0c, DAT_00352b10, DAT_00352b14,
    DAT_00352b18;

// Engine helper: float -> int quantizer (observed used for scaled float register reads)
extern unsigned long FUN_0030bd20(float value);

// Helper: map a frame pointer back to its index in the pool, with 0xEC stride
static inline unsigned long frame_ptr_to_index(uintptr_t p)
{
  if (p == 0)
    return (unsigned long)-1;
  uintptr_t base = (uintptr_t)DAT_0058beb0;
  return (unsigned long)((p - base) / 0xEC);
}

unsigned long read_script_register(unsigned long id)
{
  unsigned long out = 0;
  switch (id)
  {
  case 0x00:
    out = (unsigned long)DAT_00355044[0];
    break; // u16
  case 0x01:
    out = (unsigned long)(uint16_t)DAT_00355044[1];
    break; // u16
  case 0x02:
    out = (unsigned long)*(int *)((char *)DAT_00355044 + 0x0C);
    break; // s32
  case 0x03:
    out = (unsigned long)(uint16_t)DAT_00355044[2];
    break; // u16
  case 0x04:
    out = (unsigned long)(uint16_t)DAT_00355044[4];
    break; // u16
  case 0x05:
    out = (unsigned long)(uint16_t)DAT_00355044[3];
    break; // u16
  case 0x06:
  { // averaged halfword (signed >> 1)
    int v = (int)(int16_t)DAT_00355044[0x54];
    out = (unsigned long)(v - (v >> 31)) >> 1;
    break;
  }
  case 0x07:
    out = (unsigned long)(uint16_t)DAT_00355044[0x55];
    break; // u16
  case 0x08:
    out = (unsigned long)DAT_00355044[0x50];
    break; // u16
  case 0x09:
    out = (unsigned long)*(int *)((char *)DAT_00355044 + 0x6C);
    break; // s32 @ +0x36*2
  case 0x0A:
    out = (unsigned long)*(int *)((char *)DAT_00355044 + 0x70);
    break; // s32 @ +0x38*2
  case 0x0B:
    out = (unsigned long)*(int *)((char *)DAT_00355044 + 0x78);
    break; // s32 @ +0x3C*2
  case 0x0C:
    out = (unsigned long)*(int *)((char *)DAT_00355044 + 0x74);
    break; // s32 @ +0x3A*2

  // Scaled float reads
  case 0x0D:
    out = FUN_0030bd20(*(float *)((char *)DAT_00355044 + 0x5C) * DAT_00352adc);
    break; // +0x2E*2
  case 0x0E:
    out = FUN_0030bd20(*(float *)((char *)DAT_00355044 + 0x48) * DAT_00352ae0);
    break; // +0x24*2

  case 0x0F:
    out = (unsigned long)DAT_00355044[0x31];
    break; // u16
  case 0x10:
    out = (unsigned long)(int8_t)*(char *)((char *)DAT_00355044 + 0x94);
    break; // s8 @ +0x4A*2
  case 0x11:
    out = (unsigned long)(int8_t)*(char *)((char *)DAT_00355044 + 0x95);
    break; // s8 @ +0x95

    // 0x12: default -> 0

  case 0x13:
    out = FUN_0030bd20(*(float *)((char *)DAT_00355044 + 0x4C) * DAT_00352b04);
    break; // +0x26*2
  case 0x14:
    out = (unsigned long)DAT_00355044[0x5F];
    break; // u16
  case 0x15:
    out = (unsigned long)*(uint8_t *)((char *)DAT_00355044 + 0xBC);
    break; // u8 @ +0x5E*2
  case 0x16:
    out = (unsigned long)(uint16_t)DAT_00355044[0x61];
    break; // u16
  case 0x17:
    out = FUN_0030bd20(*(float *)((char *)DAT_00355044 + 0xC4) * DAT_00352ae4);
    break; // +0x62*2
  case 0x18:
    out = (unsigned long)DAT_00355044[0x60];
    break; // u16
  case 0x19:
    out = (unsigned long)DAT_00355044[0x30];
    break; // u16

  // Vector/transform-ish block (scaled floats)
  case 0x1A:
    out = FUN_0030bd20(*(float *)((char *)DAT_00355044 + 0x30) * DAT_00352ae8);
    break; // +0x18*2
  case 0x1B:
    out = FUN_0030bd20(*(float *)((char *)DAT_00355044 + 0x34) * DAT_00352aec);
    break; // +0x1A*2
  case 0x1C:
    out = FUN_0030bd20(*(float *)((char *)DAT_00355044 + 0x3C) * DAT_00352af0);
    break; // +0x1E*2
  case 0x1D:
    out = FUN_0030bd20(*(float *)((char *)DAT_00355044 + 0x40) * DAT_00352af4);
    break; // +0x20*2
  case 0x1E:
    out = FUN_0030bd20(*(float *)((char *)DAT_00355044 + 0x44) * DAT_00352af8);
    break; // +0x22*2
  case 0x1F:
    out = FUN_0030bd20(*(float *)((char *)DAT_00355044 + 0x154) * DAT_00352afc);
    break; // +0xAA*2
  case 0x20:
    out = FUN_0030bd20(*(float *)((char *)DAT_00355044 + 0x158) * DAT_00352b00);
    break; // +0xAC*2
  case 0x21:
    out = FUN_0030bd20(*(float *)((char *)DAT_00355044 + 0x7C) * DAT_00352b08);
    break; // +0x3E*2

  case 0x22:
    out = (unsigned long)*(uint8_t *)((char *)DAT_00355044 + 0x134);
    break; // u8 @ +0x9A*2
  case 0x23:
    out = (unsigned long)*(int *)((char *)DAT_00355044 + 0x138);
    break; // s32 @ +0x9C*2

  // Pointers -> frame index (returns -1 on null)
  case 0x24:
    out = frame_ptr_to_index((uintptr_t)*(int *)((char *)DAT_00355044 + 0x64));
    break; // +0x32*2
  case 0x25:
    out = frame_ptr_to_index((uintptr_t)*(int *)((char *)DAT_00355044 + 0xCC));
    break; // +0x66*2
  case 0x26:
    out = (unsigned long)(int8_t)*(char *)((char *)DAT_00355044 + 0xBD);
    break; // s8 @ +0xBD
  case 0x27:
    out = frame_ptr_to_index((uintptr_t)*(int *)((char *)DAT_00355044 + 0x68));
    break; // +0x34*2

  // More scaled floats
  case 0x28:
    out = FUN_0030bd20(*(float *)((char *)DAT_00355044 + 0x54) * DAT_00352b0c);
    break; // +0x2A*2
  case 0x29:
    out = FUN_0030bd20(*(float *)((char *)DAT_00355044 + 0x58) * DAT_00352b10);
    break; // +0x2C*2
  case 0x2A:
    out = FUN_0030bd20(*(float *)((char *)DAT_00355044 + 0x11C) * DAT_00352b14);
    break; // +0x8E*2
  case 0x2B:
    out = FUN_0030bd20(*(float *)((char *)DAT_00355044 + 0x120) * DAT_00352b18);
    break; // +0x90*2

  // Raw shorts/bytes at far region
  case 0x2C:
    out = (unsigned long)DAT_00355044[0x95];
    break; // u16
  case 0x2D:
    out = (unsigned long)DAT_00355044[0x94];
    break; // u16
  case 0x2E:
    out = (unsigned long)DAT_00355044[0x96];
    break; // u16
  case 0x2F:
    out = (unsigned long)DAT_00355044[0x97];
    break; // u16
  case 0x30:
    out = (unsigned long)*(uint8_t *)((char *)DAT_00355044 + 0x132);
    break; // u8 @ +0x99*2
  case 0x31:
    out = (unsigned long)DAT_00355044[0x9B];
    break; // u16
  case 0x32:
    out = (unsigned long)(int8_t)*(char *)((char *)DAT_00355044 + 0x195);
    break; // s8
  case 0x33:
    out = (unsigned long)DAT_00355044[5];
    break; // u16

  // More scaled floats (direct fields)
  case 0x34:
    out = FUN_0030bd20(*(float *)((char *)DAT_00355044 + 0x140));
    break; // +0xA0*2
  case 0x35:
    out = FUN_0030bd20(*(float *)((char *)DAT_00355044 + 0x144));
    break; // +0xA2*2
  case 0x36:
    out = FUN_0030bd20(*(float *)((char *)DAT_00355044 + 0x148));
    break; // +0xA4*2
  case 0x37:
    out = (unsigned long)(int8_t)*(char *)((char *)DAT_00355044 + 0x133);
    break; // s8 @ +0x133

  // 0x38..0x3F -> int array at base + 0xCC*2
  case 0x38:
  case 0x39:
  case 0x3A:
  case 0x3B:
  case 0x3C:
  case 0x3D:
  case 0x3E:
  case 0x3F:
    out = (unsigned long)*(int *)((char *)DAT_00355044 + 0x198 + (int)(id - 0x38) * 4);
    break;

  case 0x40:
    out = (unsigned long)*(uint8_t *)((char *)DAT_00355044 + 0x96);
    break; // u8 @ +0x4B*2

  default:
    out = 0;
    break;
  }
  return out;
}
