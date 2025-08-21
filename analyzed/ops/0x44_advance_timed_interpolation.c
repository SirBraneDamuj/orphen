// Opcodes 0x42 and 0x44 — advance_timed_interpolation (analyzed)
// Original shared handler: FUN_0025dd60
//
// Summary:
// - Evaluates a duration (expr0) and advances a fixed-point accumulator toward (duration<<5)
//   by adding the global per-tick delta each call. While in progress, it updates a set of
//   global interpolators using one of two routines depending on the opcode (0x42 vs 0x44),
//   then returns 0 to keep the VM waiting. Once the accumulator reaches the target, returns 1.
//
// Inputs:
// - expr0: duration (integer). Total duration in “tick” units; internal math uses Q5 fixed point.
//
// Globals (from raw):
// - sGpffffbd68 (s16): current opcode byte; used to select the interpolation routine.
// - iGpffffbd78 (s32): Q5 accumulator; compared against (expr0<<5) and incremented by iGpffffb64c.
// - iGpffffb64c (s32): per-frame delta in Q5 units (commonly 32 per 1.0 tick; many systems scale by 1/32).
//
// Side effects:
// - Calls FUN_00217f38 or FUN_00218158 with (iGpffffbd78 >> 5) truncated toward zero, updating global
//   render/transform state via FUN_00266ce8 and related routines.
// - Increments iGpffffbd78 by iGpffffb64c while unfinished.
//
// Notes:
// - 0x42 path calls FUN_00217f38(step).
// - 0x44 path calls FUN_00218158(step), which performs extra updates (e.g., FUN_00266988 and copies a pair
//   of globals) compared to 0x42.
// - Truncation toward zero is reproduced from the raw code via (n>=0 ? n : n+31) >> 5 before passing to the callee.
// - Return semantics (0 = in progress, 1 = done) match the VM’s cooperative wait pattern.

#include <stdint.h>

// Evaluator (reads 4-byte expression result into out[0])
extern void FUN_0025c258(int out[4]);

// Interpolator updaters (unanalyzed originals)
extern void FUN_00217f38(int step /*, int denom*/);
extern void FUN_00218158(int step /*, int denom*/);

// Globals (externed with decompiler-provided names/types)
extern short sGpffffbd68;
extern int iGpffffbd78;
extern int iGpffffb64c;

// Original signature: undefined4 FUN_0025dd60(void)
unsigned int opcode_0x44_advance_timed_interpolation(void)
{
  int args[4];
  const short op = sGpffffbd68;
  FUN_0025c258(args); // read duration into args[0]

  // Continue until accumulator reaches duration<<5 (Q5 target)
  if (iGpffffbd78 < (args[0] << 5))
  {
    // Truncate toward zero before dividing by 32
    int n = iGpffffbd78;
    int step = ((n >= 0) ? n : (n + 31)) >> 5;

    if (op == 0x42)
    {
      FUN_00217f38(step);
    }
    else if (op == 0x44)
    {
      FUN_00218158(step);
    }

    // Advance accumulator by per-tick delta and report in-progress
    iGpffffbd78 += iGpffffb64c;
    return 0;
  }

  // Done
  return 1;
}
