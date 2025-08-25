// Text opcode 0x17 — LAB_00239a00
// Raw flow:
//   prologue save RA
//   v0 = FUN_00206c28()
//   if (v0 == 0) branch to LAB_00239a20 (function epilogue/return) without modifying cursor
//   else (v0 != 0) load current cursor from gp-0x5140, increment by 1, store back
//   return
//
// Interpretation:
//   Polls the asynchronous audio/resource loader state (FUN_00206c28). If loader reports completion (returns 1),
//   advances the dialogue control cursor (gp-0x5140) by one byte (skips a placeholder / waits token). If not
//   complete (returns 0) the cursor is unchanged, effectively causing the opcode to be retried on subsequent
//   processing ticks until the load completes.
//
// Relationship to opcode 0x16:
//   Opcode 0x16 can request an audio resource with an optional blocking wait (param_3). When non-blocking is
//   used (param_3 == 0), the script likely inserts opcode 0x17 afterward to advance only when the loader reaches
//   a stable/ready state (FUN_00206c28 returns 1). This provides fine-grained pacing of voiced lines sync.
//
// Globals:
//   GP_NEG_0x5140 : dialogue/control stream cursor (incremented conditionally)
//   FUN_00206c28() external loader poll; returns 1 when idle / previous request done; 0 otherwise.
//
// Side effects:
//   Conditional +1 cursor advance; no glyph emission; no other state change.
//
// TODO:
//   - Confirm that the skipped byte (the one advanced over) is always data already consumed or a filler marker.
//   - Once gp-0x5140 symbol resolved, update naming consistently across opcode handlers.
//
// Original label preserved via wrapper structure.

#include <stdint.h>

extern int GP_NEG_0x5140; // dialogue control cursor (pending confirmed name)
extern unsigned int FUN_00206c28(void);

void text_op_17_wait_for_audio_load_step(void)
{
  if (FUN_00206c28() != 0)
  {
    GP_NEG_0x5140 += 1; // advance cursor one byte only after completion
  }
}

// Original label wrapper not needed (LAB_), dispatcher will point to this analyzed form once integrated.
