/*
 * Opcode 0x07 handler — original symbol: FUN_00239368
 *
 * Behavior:
 *   Thin wrapper that (1) invokes FUN_00238f98 to advance active dialogue glyph/banner
 *   timers for the current layer, then (2) increments the dialogue control stream cursor
 *   pointer DAT_00354e30 by 1 byte.
 *
 *   The helper FUN_00238f98 iterates over up to 300 slot records (stride 0x3C starting at
 *   iGpffffaed4) and for each active slot (byte +0x3A != 0) matching the current layer tag
 *   (byte +0x3B == cGpffffaec4) with a non‑zero countdown at offset +0x34:
 *     - Decrements the 16‑bit countdown.
 *     - If it reaches zero, clears the active flag byte (+0x3A).
 *     - Otherwise, adds 0x16 to the slot field at +0x0C (appears to be a vertical pixel
 *       position or y‑accumulator judging from other overlay setup code adding 0x16 units).
 *   It also resets uGpffffbcdc to 0 at start and enforces a pacing counter using
 *   iGpffffbcd8 / iGpffffbce0 (if iGpffffbcd8 < iGpffffbce0 it increments iGpffffbcd8 and
 *   exits early without iterating slots). This suggests a throttled per‑frame or per‑tick
 *   update frequency for these timers.
 *
 * Side Effects:
 *   - Calls FUN_00238f98 (glyph/banner slot timer advancement; global slot array mutation).
 *   - Increments DAT_00354e30 by 1. (Advances the dialogue script cursor one byte — likely
 *     skipping over an opcode parameter or moving past the opcode marker if 0x07 is a
 *     single‑byte control with no payload.)
 *
 * No additional parameters are read; no conditionals beyond what FUN_00238f98 performs.
 *
 * Open Questions:
 *   - What semantic name should replace FUN_00238f98? (Candidate: advance_active_overlay_timers)
 *   - Is the single cursor increment simply consuming a padding byte after 0x07, or is 0x07
 *     defined as a 1‑byte opcode with no operands (cursor advance emulating a no‑op)? Need
 *     to observe surrounding byte patterns in script dumps.
 *
 * References:
 *   - FUN_00238f98 decompiled in src/FUN_00238f98.c
 *   - DAT_00354e30 (dialogue stream cursor) also manipulated by opcodes 0x16/0x18/0x19 etc.
 */

// Extern the raw helper until separately analyzed / renamed.
extern void FUN_00238f98(void); /* candidate: advance_active_overlay_timers */

// Global dialogue stream cursor (gp-relative in binary); raw symbol preserved.
extern char *DAT_00354e30;

void text_op_07_advance_glyph_timers_and_cursor(void)
{
  FUN_00238f98();
  DAT_00354e30 = DAT_00354e30 + 1; // advance script cursor by 1 byte
}
