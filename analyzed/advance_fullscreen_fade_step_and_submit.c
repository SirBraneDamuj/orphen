/*
 * Advance fullscreen fade: step alpha accumulator and submit overlay
 * Original: FUN_0025d238
 *
 * Summary
 * - Advances a 16-bit accumulator toward 0x1FE0 using a per-tick rate, clamps at the cap, sets a
 *   dirty flag, optionally counts down a hold timer, and always submits a fullscreen quad with
 *   ARGB color whose alpha = (accum >> 5).
 * - Returns 1 when the fade is finished (accum reached cap and hold timer elapsed), else 0.
 *
 * Details
 * - accum (DAT_00571dd0, u16) increases by (rate * tick) each call; capped at 0x1FE0.
 * - rate (DAT_00571dd2, u16) is a multiplier in accumulator units per tick.
 * - tick (DAT_003555bc, u32) scales advance; consistent with other time-based ops.
 * - When accum < cap: mark DAT_0035505c |= 0x2 (screen/state dirty).
 * - When accum >= cap: if hold (DAT_00571dda, s16) < 1 → done flag = 1; else hold -= tick.
 * - Submits color = base (DAT_00571dd4) + ((accum >> 5) << 24) via FUN_0025d0e0(color, 1).
 *
 * PS2-specific notes
 * - Caller FUN_0025d0e0 constructs a fullscreen rectangle (-320..320, -224..224) and pushes packet
 *   0x1007; the second parameter selects a state value (0x44180 vs 0x40180).
 * - Shifting by 5 maps 0..0x1FE0 to 0..255 alpha steps.
 *
 * Original signature:
 *   undefined2 FUN_0025d238(void)
 */

#include <stdint.h>

// Globals
extern volatile uint16_t DAT_00571dd0; // alpha accumulator [0..0x1FE0]
extern volatile uint16_t DAT_00571dd2; // rate per tick
extern volatile uint32_t DAT_003555bc; // global tick/delta
extern volatile uint32_t DAT_00571dd4; // base ARGB (no alpha baked)
extern volatile int16_t DAT_00571dda;  // hold counter (frames or ticks)
extern volatile uint16_t DAT_00571dd8; // completion flag
extern volatile uint32_t DAT_0035505c; // dirty flags (OR 0x2)

// Submit fullscreen quad with color; param selects state (0x44180 vs 0x40180)
extern void FUN_0025d0e0(uint32_t argb, char state_flag);

// Analyzed wrapper with descriptive name
uint16_t advance_fullscreen_fade_step_and_submit(void)
{
  // Clear completion flag each tick
  DAT_00571dd8 = 0;

  // If not yet at cap, advance accumulator by rate * tick with clamp
  if ((int16_t)DAT_00571dd0 < 0x1FE0)
  {
    uint32_t step = (uint32_t)DAT_00571dd2 * DAT_003555bc;
    uint32_t next = (uint32_t)DAT_00571dd0 + step;
    DAT_00571dd0 = (uint16_t)next;
    // Clamp to 0x1FE0 if overflowed cap
    if (((int)(next << 16) >> 16) > 0x1FE0)
    {
      DAT_00571dd0 = 0x1FE0;
    }
    // Mark dirty
    DAT_0035505c |= 0x2;
  }
  else
  {
    // At cap: manage hold countdown and completion flag
    if (DAT_00571dda < 1)
    {
      DAT_00571dd8 = 1; // done
    }
    else
    {
      DAT_00571dda = (int16_t)(DAT_00571dda - (int16_t)DAT_003555bc);
    }
  }

  // Compose ARGB with alpha = accum >> 5 in top byte
  uint8_t alpha = (uint8_t)(DAT_00571dd0 >> 5);
  uint32_t argb = DAT_00571dd4 + ((uint32_t)alpha << 24);

  // Submit fullscreen overlay
  FUN_0025d0e0(argb, 1);

  return DAT_00571dd8;
}
