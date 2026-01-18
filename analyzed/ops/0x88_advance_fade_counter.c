// Opcode 0x88 — advance_fade_counter (analyzed)
// Original: FUN_00260cc0
//
// Summary:
// - Wrapper for FUN_0025d2f8, which decrements a fade counter and submits a fullscreen
//   view rect packet with color derived from counter position.
// - Similar to opcode 0x86 but uses a different fade counter system (DAT_00571dc0 vs DAT_00571dd0).
//
// Behavior of callee FUN_0025d2f8 (informational):
// - Globals:
//   - DAT_00571dc0 (s16): fade counter, decremented each frame
//   - DAT_00571dc2 (s16): decrement rate per tick
//   - DAT_00571dc4 (u32): base color (RGB, no alpha baked in)
//   - DAT_00571dc8 (u16): completion flag; set to 1 when fade finishes
//   - DAT_003555bc (u32): global frame delta/tick for timing
//   - DAT_0035505c (u32): status bitfield; OR'ed with 0x01 during active fade
// - Algorithm:
//   1. newCounter = DAT_00571dc0 - (DAT_00571dc2 * DAT_003555bc)
//   2. If newCounter < 0:
//      - Clamp DAT_00571dc0 to 0
//      - Set completion flag DAT_00571dc8 = 1
//   3. Else:
//      - Update DAT_00571dc0 = newCounter
//      - Set status bit: DAT_0035505c |= 1
//      - Clear completion flag DAT_00571dc8 = 0
//   4. Calculate final color = DAT_00571dc4 + ((counter >> 5) << 24)
//      - Shift right by 5 converts counter to 0-255 alpha range
//      - Shift left by 24 places alpha in upper byte (ARGB format)
//   5. Call FUN_0025d0e0(color, 1) to submit fullscreen view rect packet
//
// PS2-specific notes:
// - Counter >> 5 maps fade steps to 8-bit alpha (0x00-0xFF)
// - FUN_0025d0e0 (analyzed as build_and_submit_view_rect_packet) constructs fullscreen
//   rectangle at ±320x±224 and submits packet 0x1007
// - Second parameter (1) to FUN_0025d0e0 selects state value 0x44180
// - Completion flag (DAT_00571dc8) allows scripts to detect when fade finishes
//
// Comparison with opcode 0x86:
// - 0x86 uses DAT_00571dd0-dd8 fade system, increments toward 0x1FE0 cap
// - 0x88 uses DAT_00571dc0-dc8 fade system, decrements toward 0
// - Both submit fullscreen rects via FUN_0025d0e0 with alpha-blended color
//
// Side effects:
// - Advances fade counter globals and draws fade overlay each call until completion.
// - Opcode doesn't return a VM value.
//
// Original signature: void FUN_00260cc0(void)

#include <stdint.h>

// Fade counter advance and rect submission (callee pending separate analysis)
// Original: FUN_0025d2f8
extern void FUN_0025d2f8(void);

void opcode_0x88_advance_fade_counter(void)
{
  FUN_0025d2f8();
}
