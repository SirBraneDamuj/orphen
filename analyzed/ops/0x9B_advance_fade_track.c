// Opcode 0x9B — advance_fade_track
// Original handler: FUN_00261c38 (src/FUN_00261c38.c)
//
// Summary
//   Reads a single argument from the VM and forwards it to FUN_0025d480(index), which advances
//   a time-stepped fade/lerp track in a global table and updates output bytes (likely RGB or
//   similar channels) by blending between two key states using 1/32-step increments.
//
// Callee semantics (FUN_0025d480)
//   - param_1 selects a track; internal stride 0x14 across multiple parallel arrays at
//     base &DAT_00572078..&DAT_00572088.
//   - Computes iVar5 = ceil(current / 32) and iVar3 = current - iVar5.
//   - Blends output bytes at +0x86..+0x88 as weighted averages of two source bytes
//     (+0x80/+0x83, +0x81/+0x84, +0x82/+0x85) with weights (iVar3, iVar5) normalized by total.
//   - Increments the track's accumulator by a global step (DAT_003555bc) and returns whether the
//     track exceeded total*32 (i.e., fade complete). Also asserts if a required length field is 0.
//
// Notes
//   - We ignore the boolean return from FUN_0025d480 here; the opcode doesn’t branch on it.
//   - This opcode is a control step to progress a palette/color (or similar) fade.
//
// Original signature
//   void FUN_00261c38(void);

#include <stdint.h>
#include <stdbool.h>

// VM helper to fetch the next immediate/argument (analyzed name)
extern void bytecode_interpreter(void *out); // orig: FUN_0025c258

// Fade track stepper (see summary above)
extern bool FUN_0025d480(int trackIndex);

unsigned long opcode_0x9B_advance_fade_track(void)
{
  uint32_t arg[4]; // auStack_20
  bytecode_interpreter(arg);
  (void)FUN_0025d480((int)arg[0]);
  return 0;
}
