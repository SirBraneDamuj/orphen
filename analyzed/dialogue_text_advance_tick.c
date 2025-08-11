// Analysis of FUN_00237de8
// Original decompiled signature: void FUN_00237de8(void)
//
// Inferred Role:
//   Per-frame dialogue stream advance / text rendering tick.
//   Drives timed emission of glyphs (characters) from the current dialogue byte stream pointed to by
//   the global pbGpffffaec0, applying pacing derived from a width/time look‑up (FUN_00238e50) and
//   a global time accumulator (iGpffffbcdc) against a per-line or per-chunk budget (iGpffffbce4).
//
// Stream Encoding (as inferred here):
//   * Control opcodes: bytes < 0x1F. These are dispatched immediately through a function pointer
//     table PTR_FUN_0031c640, then the tick returns (single control processed this frame).
//   * Text/glyph bytes: bytes >= 0x1F. Most glyphs appear to range upward; a special case for byte 0x20
//     (space) triggers lookahead logic to decide if a line wrap / flush (FUN_00238f98) should occur
//     before consuming it.
//   * Timing is scaled: width_or_duration = (FUN_00238e50(opcode) * 0x5A) / 100.
//     This scaling factor (0x5A / 100 => 90/100) looks like a framerate or speed adjustment.
//
// Key Globals (names provisional):
//   pbGpffffaec0 : current byte* into dialogue stream.
//   PTR_FUN_0031c640 : dispatch table for control opcodes (<0x1F).
//   iGpffffbcdc : accumulated timing for current segment (short written, used in comparisons).
//   iGpffffbce4 : target threshold (end-of-line or wrap trigger), compared after adding glyph timing.
//   FUN_00238a08 : invoked to enqueue/render a glyph, building per-glyph metadata structures.
//   FUN_00238f98 : invoked when a timing threshold / wrap condition hit to finalize current line/segment.
//
// Space (0x20) Lookahead:
//   When encountering a space, code scans forward through subsequent glyph bytes (while they remain
//   >0x1E and not another space) accumulating their projected time. If adding those would exceed
//   remaining budget (iGpffffbce4 - iGpffffbcdc) it flushes before consuming the space, producing an early wrap.
//
// Early exits:
//   * After processing a control.
//   * After emitting a glyph if threshold reached / wrap triggered.
//   * If cumulative time for this invocation exceeds a small limit (iVar6 > 1) to cap glyphs per frame.
//   * If next byte becomes a control (<0x20) ending plain text run.
//
// Limits:
//   The loop allows at most a couple of glyph timing additions per frame (heuristic: iVar6 accumulates
//   glyph times; once >1 scaled unit it stops). This enforces animated text appearance.
//
// PS2 Considerations:
//   Using shorts for accumulators implies fixed 16-bit pacing counters, likely advanced by a vsync-based
//   timer elsewhere. Scaling by (value*0x5A)/100 uses integer math (avoids division by 0x64 explicitly).
//
// Remaining Unknowns:
//   * Exact semantics of individual control opcodes (<0x1F) dispatched via PTR_FUN_0031c640.
//   * Precise unit of FUN_00238e50(op) (width vs frame ticks). Both width and time correlation plausible.
//   * Wrap threshold origin (iGpffffbce4) initialization.
//
// Wrapper Strategy:
//   Provide a clearer named function dialogue_text_advance_tick, keep original FUN_* symbol for cross-reference.
//
// NOTE: This file is analytical; raw original remains in src/; do not modify that file directly.

#include "orphen_globals.h"

// Extern globals (types guessed; refine as more context emerges)
extern unsigned char *pbGpffffaec0; // current stream pointer
extern void *PTR_FUN_0031c640;      // base of function pointer table (code**)
extern short iGpffffbcdc;           // accumulated timing
extern short iGpffffbce4;           // timing threshold

// Forward decompiled helpers (unanalyzed wrappers elsewhere)
extern int FUN_00238e50(unsigned int opcode);      // opcode length/width lookup
extern void FUN_00238a08(unsigned char *glyphPtr); // enqueue glyph
extern void FUN_00238f98(void);                    // flush / wrap handler

static inline void dispatch_control(unsigned char opcode)
{
  // Compute entry: ((opcode << 24) >> 22) = opcode * 0x40 (since shift arithmetic?), original decompiled math:
  // ((int)((uint)*pbGpffffaec0 << 0x18) >> 0x16) -> opcode * 0x10000 >> 0x16 = opcode * 0x40.
  // So table is 0x40-byte spaced (likely a compact jump table of function pointers).
  void (**table)(void) = (void (**)(void))PTR_FUN_0031c640;
  table[(opcode & 0xFF) * 0x40 / 0x40](); // simplified indexing; keep comment for clarity.
}

void dialogue_text_advance_tick(void)
{
  if (*pbGpffffaec0 < 0x1F)
  {
    dispatch_control(*pbGpffffaec0);
    return;
  }

  int cumulativeUnits = 0; // iVar6 in original
  while (1)
  {
    unsigned char current = *pbGpffffaec0;
    if (current == 0x20)
    {
      // Lookahead to measure upcoming word (until control / space)
      unsigned char *scan = pbGpffffaec0 + 1;
      int projected = 0;
      if (pbGpffffaec0[1] > 0x1E && pbGpffffaec0[1] != 0x20)
      {
        unsigned char nxt = *scan;
        while (1)
        {
          scan++;
          int w = FUN_00238e50(nxt);
          projected += (w * 0x5A) / 100;
          if (*scan < 0x1F || *scan == 0x20)
            break;
          nxt = *scan;
        }
      }
      // If remaining budget insufficient for word, wrap before consuming space
      if (iGpffffbce4 <= iGpffffbcdc + projected)
      {
        FUN_00238f98();
        pbGpffffaec0++; // advance past space
        return;
      }
    }

    unsigned char *glyphPtr = pbGpffffaec0;
    pbGpffffaec0++;         // consume glyph
    FUN_00238a08(glyphPtr); // enqueue / render glyph
    int width = FUN_00238e50(current);
    int delta = (width * 0x5A) / 100;
    iGpffffbcdc = (short)(iGpffffbcdc + (short)delta);
    if (iGpffffbce4 <= iGpffffbcdc)
    {
      FUN_00238f98();
      return;
    }
    cumulativeUnits += delta;
    if (current == 0x20)
      break; // stop after space processed
    if (cumulativeUnits > 1)
      return; // pacing cap per tick
    if (*pbGpffffaec0 < 0x20)
      return; // next is control -> defer to next frame
  }
}

// Wrapper preserving original name
void FUN_00237de8(void)
{
  dialogue_text_advance_tick();
}
