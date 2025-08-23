// Analyzed re-expression of FUN_0025c8f8
// Original signature: undefined8 FUN_0025c8f8(undefined8 param_1, undefined8 param_2)
//
// Role (inferred):
//   Generic parameter setter for the currently "active" spawned effect / object referenced by
//   global pointer puGpffffb0d4 (declared as uint16_t* in other analyzed files). A script layer
//   (see FUN_00260360 handling opcodes 0x77..0x7C) invokes this with an attribute index (param_1)
//   and a raw integer value (param_2) or the result of arithmetic/bitwise ops performed there.
//   The setter writes the value—possibly normalized through division by per-attribute scale
//   floats or converted through FUN_00216690—into specific offsets within the object struct.
//
// Characteristics:
//   - Attribute indices observed: 0x00..0x40 (sparse; gaps: 0x12, 0x24, 0x25, 0x27, 0x31, 0x41+)
//   - Mixed field types: uint16, int32, float, byte, packed/fixed via FUN_00216690.
//   - Several attributes use per-attribute normalization denominators fGpffff8bb0..fGpffff8bf4.
//   - Index group 0x38..0x3F maps to an array of 8 int32 values starting at byte offset 0x198.
//     (Computation: base short* + ((attr-0x38)*2 + 0xCC) -> ((0xCC *2) = 0x198 byte start).
//   - Special case 0x21: if puGpffffb0d4 equals &DAT_0058beb0 (probable primary/anchor object), the
//     just-written float is propagated to a scattered set of other objects whose short IDs appear in
//     table DAT_00343692 (stride 0x28 bytes = 0x14 shorts). Each valid entry (<0x100 and != active index)
//     causes an update of (&DAT_0058bf2c)[id * 0x76] offset with that float (exact structure undisclosed).
//
// Caution:
//   - Without full struct layout we retain raw offsets but annotate byte offsets where helpful.
//   - Globals fGpffff8bac..fGpffff8bf4 appear to be per-axis or per-parameter scaling denominators.
//   - Names kept conservative: "set_active_effect_parameter" for analyzed variant.
//
// Returned Value:
//   Mirrors original behavior: returns param_2 (pass-through) as 64-bit (likely unused by callers).
//
// Side Effects:
//   - Writes into the object structure puGpffffb0d4.
//   - May propagate a value to other global objects on case 0x21.
//
// TODO:
//   - Derive semantic names for groups (position, velocity, color, timing) via cross-reference of
//     offsets in code using those fields (search for same offsets in update/render paths).
//   - Map FUN_00216690 output format (fixed-point? packed angle?).
//   - Confirm nature of the propagation in case 0x21 (shared parameter broadcast).
//
// Original function kept at bottom calling analyzed implementation for cross-reference.

#include <stdint.h>

// ===== External globals (raw names preserved) =====
extern uint16_t *puGpffffb0d4; // active object/effect base (treated as struct start)
extern float fGpffff8bac, fGpffff8bb0, fGpffff8bd4, fGpffff8bb4,
    fGpffff8bb8, fGpffff8bbc, fGpffff8bc0, fGpffff8bc4, fGpffff8bc8,
    fGpffff8bcc, fGpffff8bd0, fGpffff8bd8, fGpffff8bdc, fGpffff8be0,
    fGpffff8be4, fGpffff8be8, fGpffff8bec, fGpffff8bf0, fGpffff8bf4;
extern int DAT_0058beb0;   // primary object sentinel address
extern int DAT_0058bf2c;   // base for propagated parameter updates (stride 0x1D8? given 0x76 short* units)
extern short DAT_00343692; // table of short IDs (7 entries spaced by 0x28 bytes = 0x14 shorts) used in case 0x21

// ===== External functions =====
extern uint32_t FUN_00216690(float f); // packs/quantizes float to uint32
extern int FUN_002298d0(int);          // returns current active index (?) used in exclusion test inside case 0x21

// Helper to write 32-bit at short-based offset (attr computations sometimes treat base as short*)
static inline void write_int32_at_short_index(uint16_t *base, int shortIndex, int value)
{
  *(int *)((uint8_t *)base + shortIndex * 2) = value;
}

uint64_t set_active_effect_parameter(uint64_t attr_u64, uint64_t value_u64)
{
  uint32_t attr = (uint32_t)attr_u64;
  int val = (int)value_u64; // raw script-sourced integer
  uint16_t val16 = (uint16_t)val;
  uint8_t val8 = (uint8_t)val;

  if (!puGpffffb0d4)
    return value_u64; // nothing active (defensive; original did not guard)

  switch (attr)
  {
  case 0x00:
    puGpffffb0d4[0x00] = val16;
    break; // +0x000
  case 0x01:
    puGpffffb0d4[0x01] = val16;
    break; // +0x002
  case 0x02:
    write_int32_at_short_index(puGpffffb0d4, 0x06, val);
    break; // +0x00C
  case 0x03:
    puGpffffb0d4[0x02] = val16;
    break; // +0x004
  case 0x04:
    puGpffffb0d4[0x04] = val16;
    break; // +0x008
  case 0x05:
    puGpffffb0d4[0x03] = val16;
    break; // +0x006 (out-of-order grouping)
  case 0x06:
    puGpffffb0d4[0x54] = (uint16_t)(val << 1);
    break; // +0x0A8 scaled int*2
  case 0x07:
    puGpffffb0d4[0x55] = val16;
    break; // +0x0AA
  case 0x08:
    puGpffffb0d4[0x50] = val16;
    break; // +0x0A0
  case 0x09:
    write_int32_at_short_index(puGpffffb0d4, 0x36, val);
    break; // +0x06C
  case 0x0A:
    write_int32_at_short_index(puGpffffb0d4, 0x38, val);
    break; // +0x070
  case 0x0B:
    write_int32_at_short_index(puGpffffb0d4, 0x3C, val);
    break; // +0x078
  case 0x0C:
    write_int32_at_short_index(puGpffffb0d4, 0x3A, val);
    break; // +0x074
  case 0x0D:
    *(uint32_t *)((uint8_t *)puGpffffb0d4 + 0x5C) = FUN_00216690((float)val / fGpffff8bac);
    break; // short idx 0x2E
  case 0x0E:
    *(float *)((uint8_t *)puGpffffb0d4 + 0x48) = (float)val / fGpffff8bb0;
    break; // idx 0x24
  case 0x0F:
    puGpffffb0d4[0x31] = val16;
    break; // +0x062
  case 0x10:
    *(uint8_t *)((uint8_t *)puGpffffb0d4 + 0x94) = val8;
    break; // short idx 0x4A
  case 0x11:
    *(uint8_t *)((uint8_t *)puGpffffb0d4 + 0x95) = val8;
    break; // +0x095 (tag byte used elsewhere)
  case 0x13:
    *(float *)((uint8_t *)puGpffffb0d4 + 0x4C) = (float)val / fGpffff8bd4;
    break; // idx 0x26
  case 0x14:
    puGpffffb0d4[0x5F] = val16;
    break; // +0x0BE
  case 0x15:
    *(uint8_t *)((uint8_t *)puGpffffb0d4 + 0xBC) = val8;
    break; // idx 0x5E
  case 0x16:
    puGpffffb0d4[0x61] = val16;
    break; // +0x0C2
  case 0x17:
    *(uint32_t *)((uint8_t *)puGpffffb0d4 + 0xC4) = FUN_00216690((float)val / fGpffff8bb4);
    break; // idx 0x62
  case 0x18:
    puGpffffb0d4[0x60] = val16;
    break; // +0x0C0
  case 0x19:
    puGpffffb0d4[0x30] = val16;
    break; // +0x060
  case 0x1A:
    *(float *)((uint8_t *)puGpffffb0d4 + 0x30) = (float)val / fGpffff8bb8;
    break; // idx 0x18
  case 0x1B:
    *(float *)((uint8_t *)puGpffffb0d4 + 0x34) = (float)val / fGpffff8bbc;
    break; // idx 0x1A
  case 0x1C:
    *(float *)((uint8_t *)puGpffffb0d4 + 0x3C) = (float)val / fGpffff8bc0;
    break; // idx 0x1E
  case 0x1D:
    *(float *)((uint8_t *)puGpffffb0d4 + 0x40) = (float)val / fGpffff8bc4;
    break; // idx 0x20
  case 0x1E:
    *(float *)((uint8_t *)puGpffffb0d4 + 0x44) = (float)val / fGpffff8bc8;
    break; // idx 0x22
  case 0x1F:
    *(uint32_t *)((uint8_t *)puGpffffb0d4 + 0x154) = FUN_00216690((float)val / fGpffff8bcc);
    break; // idx 0xAA
  case 0x20:
    *(uint32_t *)((uint8_t *)puGpffffb0d4 + 0x158) = FUN_00216690((float)val / fGpffff8bd0);
    break; // idx 0xAC
  case 0x21:
  {
    *(float *)((uint8_t *)puGpffffb0d4 + 0x7C) = (float)val / fGpffff8bd8; // idx 0x3E
    if ((uint16_t *)(&DAT_0058beb0) == puGpffffb0d4)
    {
      int activeIndex = FUN_002298d0(DAT_0058beb0);
      short *idPtr = &DAT_00343692; // 7 entries
      for (int i = 0; i < 7; ++i, idPtr += 0x14)
      {
        short id = *idPtr;
        if (i != activeIndex && id < 0x100)
        {
          *(float *)((uint8_t *)&DAT_0058bf2c + id * 0xEC) = *(float *)((uint8_t *)puGpffffb0d4 + 0x7C);
          // NOTE: stride derivation uncertain (raw used id*0x76 short units = id*0xEC bytes).
        }
      }
      return value_u64; // early return mirrors original special-case return
    }
  }
  break;
  case 0x22:
    *(uint8_t *)((uint8_t *)puGpffffb0d4 + 0x134) = val8;
    break; // idx 0x9A
  case 0x23:
    write_int32_at_short_index(puGpffffb0d4, 0x9C, val);
    break; // +0x138
  case 0x26:
    *(uint8_t *)((uint8_t *)puGpffffb0d4 + 0x17A) = val8;
    break; // idx 0xBD
  case 0x28:
    *(float *)((uint8_t *)puGpffffb0d4 + 0x54) = (float)val / fGpffff8bdc;
    break; // idx 0x2A
  case 0x29:
    *(float *)((uint8_t *)puGpffffb0d4 + 0x58) = (float)val / fGpffff8be0;
    break; // idx 0x2C
  case 0x2A:
    *(float *)((uint8_t *)puGpffffb0d4 + 0x11C) = (float)val / fGpffff8be4;
    break; // idx 0x8E
  case 0x2B:
    *(float *)((uint8_t *)puGpffffb0d4 + 0x120) = (float)val / fGpffff8be8;
    break; // idx 0x90
  case 0x2C:
    puGpffffb0d4[0x95] = val16;
    break; // +0x12A
  case 0x2D:
    puGpffffb0d4[0x94] = val16;
    break; // +0x128
  case 0x2E:
    puGpffffb0d4[0x96] = val16;
    break; // +0x12C
  case 0x2F:
    puGpffffb0d4[0x97] = val16;
    break; // +0x12E
  case 0x30:
    *(uint8_t *)((uint8_t *)puGpffffb0d4 + 0x132) = val8;
    break; // idx 0x99
  case 0x32:
    *(uint8_t *)((uint8_t *)puGpffffb0d4 + 0x32A) = val8;
    break; // idx 0x195
  case 0x33:
    puGpffffb0d4[0x05] = val16;
    break; // +0x00A
  case 0x34:
    *(float *)((uint8_t *)puGpffffb0d4 + 0x140) = (float)val / fGpffff8bec;
    break; // idx 0xA0
  case 0x35:
    *(float *)((uint8_t *)puGpffffb0d4 + 0x148) = (float)val / fGpffff8bf0;
    break; // idx 0xA2
  case 0x36:
    *(float *)((uint8_t *)puGpffffb0d4 + 0x150) = (float)val / fGpffff8bf4;
    break; // idx 0xA4
  case 0x37:
    *(uint8_t *)((uint8_t *)puGpffffb0d4 + 0x266) = val8;
    break; // idx 0x133
  case 0x38:
  case 0x39:
  case 0x3A:
  case 0x3B:
  case 0x3C:
  case 0x3D:
  case 0x3E:
  case 0x3F:
  {
    // Array of 8 int32 values at byte offset 0x198 + (attr-0x38)*4
    int idx = (int)attr - 0x38;
    *(int *)((uint8_t *)puGpffffb0d4 + 0x198 + idx * 4) = val;
  }
  break;
  case 0x40:
    *(uint8_t *)((uint8_t *)puGpffffb0d4 + 0x96) = val8;
    break; // short idx 0x4B
  default:
    break; // unhandled / unknown attribute
  }

  return value_u64;
}

// Original symbol preserved for cross-reference
uint64_t FUN_0025c8f8(uint64_t p1, uint64_t p2) { return set_active_effect_parameter(p1, p2); }
