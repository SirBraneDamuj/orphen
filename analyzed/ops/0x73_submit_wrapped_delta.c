// Opcode 0x73 — submit_wrapped_delta (analyzed)
// Original: FUN_00260188
//
// Summary:
// - Evaluates two VM expressions (current and target values in same domain),
//   normalizes them by DAT_00352c00, computes a wrapped delta = wrap(target - current)
//   via FUN_002166e8 (which internally calls FUN_00216690 for periodic wrapping),
//   rescales by DAT_00352c00, and submits via FUN_0030bd20.
//
// Inputs (from VM stack via expression evaluator):
//   - a: int (treated as float)
//   - b: int (treated as float)
//
// Behavior:
//   delta_norm = wrap((b/scale) - (a/scale))
//   out = delta_norm * scale
//   submit(out)
//
// Side effects:
//   - Calls FUN_0030bd20(out) to push an engine parameter (float→int32 saturating conversion).
//
// Globals (see globals.json):
//   - DAT_00352c00: float domain scale for this op’s parameter space (sibling to 0x72’s DAT_00352bfc).
//
// Notes:
//   - FUN_002166e8: small helper performing wrap(param2 - param1) using FUN_00216690’s periodic range.
//   - The wrap range is defined by globals behind FUN_00216690 (DAT_00352188/8C/90); likely an angular domain.
//   - We keep callee names as original FUN_* per policy until they are separately analyzed.

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Wrapped difference helper: returns wrap(param_2 - param_1)
extern float FUN_002166e8(float a, float b);

// Submit parameter (float→int32 saturating conversion and dispatch)
extern int FUN_0030bd20(float value);

// Globals
extern float DAT_00352c00; // scale for this domain

// Original signature: void FUN_00260188(void)
void opcode_0x73_submit_wrapped_delta(void)
{
  int a_i, b_i;
  bytecode_interpreter(&a_i);
  // The original calls the evaluator again with (ptr|4) to fill the adjacent slot; we read into b_i.
  bytecode_interpreter(&b_i);

  float scale = DAT_00352c00;
  float delta_norm = FUN_002166e8((float)a_i / scale, (float)b_i / scale);
  float out = delta_norm * scale;
  (void)FUN_0030bd20(out);
}
