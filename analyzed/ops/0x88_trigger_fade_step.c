// Opcode 0x88 — trigger_fade_step
// Original: FUN_00260cc0
//
// Summary:
// - Calls FUN_0025d2f8 to advance fade/transition state
// - No parameters read from VM
// - Simple wrapper around fade stepper function
//
// Side effects:
// - Advances global fade state via FUN_0025d2f8
// - Likely updates fade counters, colors, or timing
//
// PS2-specific notes:
// - Part of screen transition/fade system
// - Related to fullscreen fade system (see 0x86)
// - FUN_0025d2f8 is fade stepper (not yet analyzed)

#include <stdint.h>

// Fade/transition stepper function
extern void FUN_0025d2f8(void);

// Original signature: void FUN_00260cc0(void)
void opcode_0x88_trigger_fade_step(void)
{
  FUN_0025d2f8();
}

// Original signature preserved for cross-reference
// void FUN_00260cc0(void)
