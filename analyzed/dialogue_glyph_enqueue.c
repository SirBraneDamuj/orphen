// Analysis of FUN_00238a08
// Original decompiled signature: void FUN_00238a08(byte *param_1)
//
// Inferred Role:
//   Enqueue / layout a single dialogue glyph (character / symbol) into a render list or sprite batch.
//   Builds an entry in a global array piGpffffaed4 of fixed-size records (size 0x3C bytes? inferred by stride 0x3C or 0x3C/0xF ints).
//   Associates timing, position, atlas indices, palette/state, and width metrics with the glyph.
//
// Observed Behavior:
//   1. Early abort if neither of two resource/state checks (FUN_00266368(0x509) / 0x50a) succeed.
//   2. Scans glyph entry list for the first slot whose status byte at +0x3A is 0.
//      - If >299 entries scanned without a free slot, triggers fatal handler FUN_0026bfc0(0x34c420).
//   3. Marks slot active, stores timing baseline (sGpffffbcdc) and vertical offset (sGpffffbcd8).
//      If some state (iGpffffbcd0 && sGpffffbcd8) is true, adds 10 to timing baseline (delayed reveal?).
//   4. Sets *slot = 0x2E (possibly base sprite ID or primitive code) then computes X/Y positioning:
//        X: iGpffffbccc + (short)slot[0xd] * -0x16 (character column * glyph width step)
//        Y: iGpffffbcc8 + baseline + 8
//      Adjusts X if iGpffffb0e4 > 0 and alignment center/left codes (0xD0 / -0x78) apply.
//   5. Stores current speaker / style (?) uGpffffaec4 at +0x3B, and a context value iGpffffbcd4 at slot[0xC].
//   6. Derives atlas coordinates from glyph code: ((code-0x20)/0xB)*0x16 and ((code-0x20)%0xB)*0x16.
//      Applies overflows >0xF1 adjustment (multi-page font atlas handling), incrementing base sprite code.
//   7. Uses width lookup FUN_00238e50 to store raw width and scaled timing values:
//        slot[4] = (width * 0x5A) / 100   (scaled width/time?)
//        slot[8] = width (original value)
//        slot[5] = 0x16, slot[9] = 0x16 (glyph height / cell size constants)
//
// Key Globals (provisional names):
//   piGpffffaed4 : pointer array / base to glyph entries (each 0x3C? stride 0x3C => 0xF ints observed).
//   sGpffffbcdc  : global timing accumulator for current line.
//   sGpffffbcd8  : vertical line offset index (line number).
//   iGpffffbccc  : horizontal origin / alignment base.
//   iGpffffbcc8  : vertical origin.
//   uGpffffaec4  : style / palette / speaker id byte.
//   iGpffffbcd4  : contextual value (maybe current dialogue window id).
//   iGpffffb0e4  : mode flag (enables alignment adjustment).
//   iGpffffbcd0  : gating flag for delayed timing.
//
// Unknowns / To Refine:
//   - Exact record structure layout (naming fields).
//   - Meaning of base code 0x2E vs 0x2F when atlas row overflow occurs.
//   - Distinction between fields at indices 4 and 8 (time vs width) and why both retained.
//
// Future naming (tentative): dialogue_enqueue_glyph
// Original name retained for cross-reference.
//
// NOTE: Analytical file; original raw decompiled function lives in src/ and remains unchanged.

#include "orphen_globals.h"

// External symbols (types guessed; refine as more info arises)
extern int *piGpffffaed4;         // glyph records base
extern short sGpffffbcdc;         // current timing baseline
extern short sGpffffbcd8;         // current line index
extern int iGpffffbccc;           // horizontal origin
extern int iGpffffbcc8;           // vertical origin
extern unsigned char uGpffffaec4; // style / speaker id
extern int iGpffffbcd4;           // context id
extern int iGpffffb0e4;           // alignment/mode flag
extern int iGpffffbcd0;           // gating flag for delay adjustment

extern long FUN_00266368(int);         // resource/state check
extern void FUN_0026bfc0(int);         // fatal / logging
extern int FUN_00238e50(unsigned int); // length/width lookup

void dialogue_enqueue_glyph(unsigned char *glyphCodePtr)
{
  // Resource availability gate
  long ok = FUN_00266368(0x509);
  if (ok == 0)
  {
    ok = FUN_00266368(0x50A);
    if (ok == 0)
      return; // neither resource available
  }

  // Find free slot (status byte at +0x3A == 0)
  int idx = 0;
  int *slot = piGpffffaed4;
  while (*(unsigned char *)((char *)slot + 0x3A) != 0)
  {
    slot += 0xF; // advance by 15 ints
    if (++idx > 299)
    {                         // safety consistent with original (>=300 entries)
      FUN_0026bfc0(0x34C420); // panic / overflow
      return;
    }
  }

  *(unsigned char *)((char *)slot + 0x3A) = 1;   // mark active
  *(short *)((char *)slot + 0x36) = sGpffffbcdc; // time baseline
  *(short *)(slot + 0xD) = sGpffffbcd8;          // vertical line index
  if (iGpffffbcd0 != 0 && sGpffffbcd8 != 0)
  { // delayed reveal condition
    *(short *)((char *)slot + 0x36) = sGpffffbcdc + 10;
  }

  slot[0] = 0x2E; // base sprite / primitive code

  int x = iGpffffbccc + (short)slot[0xD] * -0x16;            // column-based offset ( -0x16 per line index )
  int y = iGpffffbcc8 + *(short *)((char *)slot + 0x36) + 8; // vertical baseline + adjustment
  slot[2] = y;
  slot[3] = x;

  if (iGpffffb0e4 > 0)
  { // alignment adjustments
    if (iGpffffbccc == 0xD0)
    {
      x -= 0x2D;
    }
    else if (iGpffffbccc == -0x78)
    {
      x += 0x1E;
    }
    slot[3] = x;
  }

  *(unsigned char *)((char *)slot + 0x3B) = uGpffffaec4; // style / speaker id
  slot[0xC] = iGpffffbcd4;                               // context value

  // Font atlas coordinate derivation
  unsigned char g = *glyphCodePtr;
  int glyphIndex = g - 0x20;
  int atlasY = (glyphIndex / 0x0B) * 0x16; // row
  int atlasX = (glyphIndex % 0x0B) * 0x16; // column
  slot[7] = atlasY;
  slot[6] = atlasX;

  if (atlasY > 0xF1)
  { // overflow adjust
    int adj = atlasY + 0x0E;
    int base = atlasY + 0x10D;
    if (adj >= 0)
      base = adj;                           // choose smaller positive? (replicates original logic)
    slot[0] = slot[0] + 1;                  // increment base sprite code
    slot[7] = adj + ((base >> 8) * -0x100); // wrap row into limited range
  }

  int width = FUN_00238e50(g);
  slot[4] = (width * 0x5A) / 100; // scaled timing / width
  slot[5] = 0x16;                 // cell height
  slot[9] = 0x16;                 // maybe advance / bounding height
  slot[8] = width;                // original width metric
}

// Wrapper preserving original name
void FUN_00238a08(unsigned char *param_1)
{
  dialogue_enqueue_glyph(param_1);
}
