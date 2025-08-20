// Opcode 0x72 — lerp_wrap_and_submit (analyzed)
// Original: FUN_002600c8
//
// Summary:
// - Evaluates three VM expressions: start, target, and rate.
// - Converts them into a normalized domain by dividing by DAT_00352bfc, then wraps
//   start/target into the valid range using FUN_00216690 (periodic wrap around a lower/upper bound).
// - Advances start toward target by a step = (rate/DAT_00352bfc) * (DAT_003555bc * 1/32).
//   Uses FUN_0023a320 for slope-limited interpolation toward the target (clamped; no overshoot).
// - Scales the interpolated normalized result back up by DAT_00352bfc and submits it via FUN_0030bd20
//   (likely a float->fixed conversion and parameter submission into an engine subsystem).
//
// Inputs (from VM stack via expression evaluator):
//   - start:   int (treated as float)
//   - target:  int (treated as float)
//   - rate:    int (treated as float), per-tick scalar before global time scaling
//
// Side effects:
//   - Calls FUN_0030bd20(result) to update an engine parameter/register. No explicit VM return value.
//
// Globals (see globals.json):
//   - DAT_00352bfc (0x00352bfc): float scale factor for this parameter's domain.
//   - DAT_003555bc (0x003555bc): global tick/delta accumulator; time scale uses (DAT_003555bc * 1/32).
//
// Notes:
//   - FUN_00216690 behaves like a domain wrap/normalize: repeatedly adds/subtracts a period until the
//     value falls within [lower, upper]. Many call sites cast its return to float.
//   - FUN_0023a320 appears to be a monotonic approach/lerp-toward-target with a step factor; other sites
//     use it with (float)DAT_003555bc * K as the third parameter.
//   - We keep callee names as original FUN_* per project policy until analyzed separately.
//
// Contract:
//   - Pops 3 expressions; pushes nothing. Submits one param to the engine.

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out); // orig FUN_0025c258

// Periodic wrap/normalize (returns adjusted value)
extern float FUN_00216690(float value);

// Slope-limited interpolation toward target (returns adjusted/current value)
extern float FUN_0023a320(float current_wrapped,
                          float target_wrapped,
                          float step);

// Submit parameter (likely float->fixed and dispatch to a subsystem)
extern int FUN_0030bd20(float value);

// Globals
extern float DAT_00352bfc;        // scale for this domain
extern unsigned int DAT_003555bc; // global tick/delta (scaled by 1/32)

// Original signature: void FUN_002600c8(void)
void opcode_0x72_lerp_wrap_and_submit(void)
{
  // Read three VM-evaluated values: start, target, rate
  int start_i, target_i, rate_i;
  bytecode_interpreter(&start_i);
  bytecode_interpreter(&target_i);
  bytecode_interpreter(&rate_i);

  float scale = DAT_00352bfc;
  float start_norm = (float)start_i / scale;
  float target_norm = (float)target_i / scale;

  // Normalize into domain via periodic wrap
  float start_w = FUN_00216690(start_norm);
  float target_w = FUN_00216690(target_norm);

  // Time-scaled step: (rate/scale) * (DAT_003555bc * 1/32)
  float step = ((float)rate_i / scale) * 0.03125f * (float)DAT_003555bc;

  // Interpolate toward target, then scale back and submit
  float out_norm = FUN_0023a320(start_w, target_w, step);
  float out = out_norm * scale;
  (void)FUN_0030bd20(out);
}
