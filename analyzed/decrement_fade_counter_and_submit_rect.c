/*
 * Decrement fade counter and submit view rect
 * Original: FUN_0025d2f8
 *
 * Summary
 * - Decrements a 16-bit fade counter by (rate * tick_delta), clamps to 0, manages completion
 *   flags, and submits a fullscreen view rect packet with alpha-blended color.
 * - Counter decreases from initial value toward 0, opposite of FUN_0025d238's incrementing fade.
 *
 * Details
 * - counter (DAT_00571dc0, s16) decreases by (rate * tick) each call; clamped at 0.
 * - rate (DAT_00571dc2, s16) is decrement multiplier per tick.
 * - tick (DAT_003555bc, u32) is global frame delta for timing consistency.
 * - When counter reaches 0: sets completion flag DAT_00571dc8 = 1.
 * - While counter > 0: sets status bit DAT_0035505c |= 1, clears DAT_00571dc8 = 0.
 * - Submits color = base (DAT_00571dc4) + ((counter >> 5) << 24) via FUN_0025d0e0(color, 1).
 *
 * PS2-specific notes
 * - Shifting by 5 maps counter range to 0..255 alpha steps (upper byte of ARGB).
 * - FUN_0025d0e0 (analyzed as build_and_submit_view_rect_packet) constructs fullscreen
 *   rectangle at ±320x±224 and submits packet 0x1007.
 * - Second parameter (1) selects state value 0x44180 in the callee.
 *
 * Original signature:
 *   void FUN_0025d2f8(void)
 */

#include <stdint.h>

// Globals (original names from globals.json)
extern volatile int16_t DAT_00571dc0;  // fade counter [initial_value..0]
extern volatile int16_t DAT_00571dc2;  // decrement rate per tick
extern volatile uint32_t DAT_003555bc; // global tick/delta
extern volatile uint32_t DAT_00571dc4; // base RGB color (no alpha)
extern volatile uint16_t DAT_00571dc8; // completion flag (1 = finished)
extern volatile uint32_t DAT_0035505c; // status bitfield (OR 0x1 during fade)

// Submit fullscreen quad with color; second param selects state
// Analyzed: see analyzed/build_and_submit_view_rect_packet.c
extern void FUN_0025d0e0(uint32_t argb, int state_flag);

// Analyzed implementation
void decrement_fade_counter_and_submit_rect(void)
{
  // Calculate new counter value: current - (rate * tick_delta)
  int32_t newCounter = (int32_t)DAT_00571dc0 - ((int32_t)DAT_00571dc2 * (int32_t)DAT_003555bc);

  if (newCounter < 0)
  {
    // Counter reached zero - fade complete
    DAT_00571dc0 = 0;
    DAT_00571dc8 = 1; // set completion flag
  }
  else
  {
    // Still fading - update counter
    DAT_00571dc0 = (int16_t)newCounter;
    DAT_0035505c |= 1; // mark fade active in status bitfield
    DAT_00571dc8 = 0;  // clear completion flag
  }

  // Calculate alpha from counter position (counter >> 5 gives 0-255 range)
  // Place alpha in upper byte: (alpha << 24) | RGB
  uint32_t alpha = ((uint32_t)DAT_00571dc0 >> 5) & 0xFF;
  uint32_t finalColor = DAT_00571dc4 + (alpha << 24);

  // Submit fullscreen view rect packet with alpha-blended color
  FUN_0025d0e0(finalColor, 1);
}

// Original FUN_0025d2f8 exposed as:
void FUN_0025d2f8(void)
{
  decrement_fade_counter_and_submit_rect();
}
