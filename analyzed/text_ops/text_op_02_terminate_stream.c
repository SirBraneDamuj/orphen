// Text opcode 0x02 (LAB_00239328)
// Original bytes at 0x00239328:
//   sw   zero, -0x5140(gp)
//   j    FUN_00237b38
//   move a0, zero
//
// Behavior:
//   1. Writes 0 to a global at gp-0x5140 (exact symbol unresolved; TODO: map this address using globals.json).
//      Given usage, likely a per-dialogue counter or state flag (e.g., active line count / scroll state) that
//      needs clearing before termination.
//   2. Calls FUN_00237b38(0) — the analyzed dialogue_start_stream termination path — which:
//        - Clears/sets global flags (enables termination state bits 0x6000 in uGpffffb0f4, disables 0x8FF/0x8FE)
//        - Nulls the active stream pointer.
//
// Net effect: Immediate dialogue stream termination opcode embedded in text stream.
// No glyph emission; control returns early from dialogue_text_advance_tick after this control executes.
//
// Side effects summary:
//   - Zeroes unknown global at (gp-0x5140) [TODO identify]
//   - Invokes full termination routine (clears active pointer, toggles flags, resets counters)
//
// Caution: Avoid assigning a semantic name to the cleared global until cross-referenced.
//
// Wrapper provided for analysis consistency.

#include <stdint.h>

// Extern termination routine (already analyzed as dialogue_start_stream / FUN_00237b38)
extern void FUN_00237b38(long ptr);

// Extern placeholder for the gp-relative global. Replace once resolved.
extern int GP_NEG_0x5140; // TODO: rename when symbol identified

void text_op_02_terminate_stream(void)
{
  GP_NEG_0x5140 = 0; // sw zero,-0x5140(gp)
  FUN_00237b38(0);   // terminate dialogue stream
}
