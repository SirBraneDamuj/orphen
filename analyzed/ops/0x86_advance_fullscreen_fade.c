// Opcode 0x86 — advance_fullscreen_fade (analyzed)
// Original: FUN_00260ca0
//
// Summary:
// - Forwards to FUN_0025d238, which steps a fullscreen color fade by advancing an alpha accumulator
//   and immediately submits a fullscreen render packet via FUN_0025d0e0.
//
// Behavior of the callee (informational; callee kept as FUN_* until separately analyzed):
// - Globals:
//   - DAT_00571dd0 (u16): alpha accumulator in range [0 .. 0x1fe0]; maps to 0..255 via >>5.
//   - DAT_00571dd2 (u16): fade rate multiplier (units per tick).
//   - DAT_003555bc (u32): global tick/delta; used to scale step.
//   - DAT_00571dd4 (u32): base ARGB color; computed color = base + ((accum>>5)<<24).
//   - DAT_00571dda (s16): delay/hold counter after reaching max; decremented per tick.
//   - DAT_00571dd8 (u16): completion flag set to 1 when finished.
//   - DAT_0035505c (u32): OR’ed with 0x2 to mark screen/state dirty.
// - When accum < 0x1fe0: accum += rate * tick, clamped to 0x1fe0; marks dirty.
// - Else: if hold < 1 → sets completion flag; else decrements hold by tick.
// - Builds ARGB = base + ((accum >> 5) * 0x1000000), calls FUN_0025d0e0(argb, 1) which
//   configures a fullscreen rectangle (-320..320, -224..224) and submits a render packet (id 0x1007).
//
// Side effects:
// - Advances fade globals and draws the fade overlay each call until completion.
// - Opcode itself doesn’t return a VM value; it just triggers the effect.
//
// Notes:
// - FUN_0025d0e0 sets up vertex/packet data at DAT_00355724, writes repeated color and fixed rect
//   extents, toggles a state word (0x40180/0x44180) based on its second arg, and submits with FUN_00207de8(0x1007).
// - The constants 320 and 224 match the typical PS2 frame extents, confirming fullscreen coverage.

// Analyzed callee: see analyzed/advance_fullscreen_fade_step_and_submit.c
extern unsigned short advance_fullscreen_fade_step_and_submit(void);

// Original signature: void FUN_00260ca0(void)
void opcode_0x86_advance_fullscreen_fade(void)
{
  (void)advance_fullscreen_fade_step_and_submit();
}
