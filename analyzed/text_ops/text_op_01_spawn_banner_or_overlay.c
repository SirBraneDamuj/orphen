// Text opcode 0x01 (also 0x03,0x04,0x05) — FUN_002391d0
// Role:
//   Allocates/activates a display slot (likely a banner / overlay panel) in the same glyph-slot arena
//   used by dialogue glyphs, but with distinct fixed parameters (0x42A type marker, fixed dimensions,
//   timing fields) rather than per-character source-based metrics. It sets a global pointer
//   DAT_00355c60 to the created slot and zeroes DAT_00355c5c beforehand (status/reset flag).
//
// Gate:
//   Early exit unless either flag 0x509 or 0x50A is active (FUN_00266368). This mirrors glyph enqueue.
//
// Slot Search:
//   Linear scan over 300 slot structures at DAT_00354e44, looking for first with (slot+0x3A)==0.
//
// Initialization Highlights (on found slot):
//   active flag (+0x3A) = 1
//   start time (+0x36)   = DAT_00355c4c (or +0x14 if DAT_00355c40 && DAT_00355c48 non-zero)
//   line index (+0x34?)  = DAT_00355c48
//   base position X      = DAT_00355c38 + start_time + 0x10 (stored at slot[2])
//   base position Y      = DAT_00355c3c + line_index*-0x16 (adjusted for screen mode offsets)
//   slot[0] (type/id)    = 0x42A
//   layer/tag (+0x3B)    = DAT_00354e34
//   geometry metrics: slot[6]=0x60, slot[7]=0x20, slot[4]=0x14, slot[5]=0x16
//   timing? slot[8]=0xF, slot[9]=0xF
//   color slot[0xC]      = 0x80608060 if *DAT_00354e30 != 1 else 0x80808080
//   DAT_00355c60 updated to point at the slot; DAT_00355c5c reset earlier.
//
// Notes:
//   - Differences in color constant imply a mode flag at *DAT_00354e30 controlling palette/tone.
//   - The 0x42A marker distinguishes this control entity from regular glyphs (glyphs used 0x2E in FUN_00238a08).
//   - Op-codes 0x03–0x05 jump to same implementation; they may be aliases (legacy or reserving variants).
//
// TODO:
//   - Identify formal names for DAT_00355c38 / 3C / 40 / 48 / 4C configuration globals.
//   - Confirm semantic of slots[8] & [9] (lifespan frames? fade parameters?).
//   - Cross-check with rendering path (FUN_00239020 consumers) for usage of type 0x42A.
//
// Side effects:
//   - Mutates global slot array, sets global DAT_00355c60, clears DAT_00355c5c.
//   - No emission if flags (0x509/0x50A) not set or all slots occupied.

#include <stdint.h>

extern long FUN_00266368(int index);

// Globals (provisional externs; keep raw names)
extern int DAT_00355c5c;           // status/reset flag cleared
extern int *DAT_00355c60;          // pointer to allocated slot
extern int DAT_00355c4c;           // timing base
extern int DAT_00355c48;           // line index / vertical unit
extern int DAT_00355c40;           // conditional timing adjustment flag
extern int DAT_00355c3c;           // base Y reference
extern int DAT_00355c38;           // base X reference
extern int _DAT_00355054;          // screen mode? affects Y adjustment
extern unsigned char DAT_00354e34; // layer/tag
extern char *DAT_00354e30;         // mode flag pointer for color selection
extern int *DAT_00354e44;          // slot array base (300 * stride 0x3C)

void text_op_01_spawn_banner_or_overlay(void)
{
  DAT_00355c5c = 0;
  DAT_00355c60 = 0;
  if (FUN_00266368(0x509) == 0 && FUN_00266368(0x50A) == 0)
    return; // not enabled

  int *slot = DAT_00354e44;
  for (int i = 0; i < 300; ++i, slot += 0x0F)
  {
    if (*(unsigned char *)((char *)slot + 0x3A) == 0)
    {
      // Activate
      *(unsigned char *)((char *)slot + 0x3A) = 1;
      short startTime = (short)DAT_00355c4c;
      short lineIdx = (short)DAT_00355c48;
      if (DAT_00355c40 != 0 && lineIdx != 0)
        startTime = (short)(startTime + 0x14);
      *(short *)((char *)slot + 0x36) = startTime;
      *(short *)(slot + 0x0D) = lineIdx;

      int baseY = DAT_00355c3c + lineIdx * -0x16;
      int adjMode = _DAT_00355054;
      // X position
      slot[2] = DAT_00355c38 + startTime + 0x10;
      slot[3] = baseY; // provisional
      if (adjMode > 0)
      {
        int y = baseY - 0x2D;
        if (DAT_00355c3c == 0xD0)
          ; // keep y as baseY-0x2D
        else if (DAT_00355c3c == -0x78)
          y = baseY + 0x1E;
        slot[3] = y;
      }

      slot[0] = 0x42A;                                        // type marker
      *(unsigned char *)((char *)slot + 0x3B) = DAT_00354e34; // layer/tag
      slot[6] = 0x60;
      slot[7] = 0x20;
      slot[4] = 0x14;
      slot[5] = 0x16;
      slot[8] = 0x0F;
      slot[9] = 0x0F;
      slot[0x0C] = (*DAT_00354e30 != 1) ? 0x80608060 : 0x80808080;
      DAT_00355c60 = slot;
      return;
    }
  }
}

// Original symbol preserved
void FUN_002391d0(void) { text_op_01_spawn_banner_or_overlay(); }
