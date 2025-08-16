// Opcode 0x91 — param_ramp_current_toward_target (analyzed)
// Original: FUN_002611b8
//
// Summary:
// - Evaluates one expression (index) via the main VM and uses it to address a 3-float
//   per-index parameter block at DAT_00571de0 (stride 0x0C):
//     +0x00: current value, +0x04: target value, +0x08: step per tick
// - Advances current toward target by step * (DAT_003555bc * 0.03125f), preserving sign of step.
// - Clamps to target when passing overshoot threshold; returns 0 while moving, 1 when reached.
//
// Cluster context:
// - 0x61100 (setter used elsewhere): writes current/target/step for an index; flips step sign if target < current.
// - 0x91 (this): ramp current toward target.
// - 0x92: commonly submits the current value (scaled by DAT_00352c30) into an engine subsystem via FUN_0030bd20
//         (e.g., audio parameter update). Pairing frequency suggests DAT_00571de0 primarily backs audio params,
//         but this opcode itself is a general param tween over that table.
//
// Notes:
// - DAT_003555bc appears to be a tick/delta accumulator (int) scaled by 1/32 for time-based stepping.
// - Keep unresolved externs by their original labels for traceability.

#include <stdint.h>

typedef unsigned int uint;

// VM entry (analyzed name)
extern void bytecode_interpreter(void *result_out); // orig FUN_0025c258

// Param table: 3 floats per index: current(+0), target(+4), step(+8)
extern unsigned char DAT_00571de0[];

// Global tick/delta used in stepping (scaled by 1/32)
extern unsigned int DAT_003555bc;

// Original signature: undefined4 FUN_002611b8(void) — returns 1 if already at target, else 0 after stepping
unsigned int opcode_0x91_param_ramp_current_toward_target(void)
{
  // Evaluate index expression via VM
  uint idx;
  bytecode_interpreter(&idx);

  // Locate 3-float block for this index
  float *base = (float *)(DAT_00571de0 + (idx * 0x0C));
  float current = base[0]; // +0x00
  float target = base[1];  // +0x04
  float step = base[2];    // +0x08 (signed)

  if (current != target)
  {
    float dt = (float)DAT_003555bc * 0.03125f; // * (1/32)
    float next = current + step * dt;
    // Clamp toward target depending on step sign
    if (step > 0.0f)
    {
      if (next >= target)
        next = target;
    }
    else
    {
      if (next <= target)
        next = target;
    }
    base[0] = next;
    return 0; // still moving (or just clamped this tick)
  }

  return 1; // already at target
}
