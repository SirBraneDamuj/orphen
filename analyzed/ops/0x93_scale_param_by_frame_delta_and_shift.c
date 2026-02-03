// Opcode 0x93 — scale_param_by_frame_delta_and_shift
// Original: FUN_002612a0
//
// Summary:
// - Reads one parameter from VM
// - Multiplies by frame delta (DAT_003555bc)
// - Divides by 32 (right shift 5 bits)
// - Returns scaled result
//
// Parameters:
// - param0: Value to scale
//
// Return value:
// - (param0 * DAT_003555bc) >> 5
//
// Frame-rate independence:
// DAT_003555bc appears to be a frame time/delta value.
// Division by 32 (0x1f mask for rounding, then >> 5) normalizes
// the timing calculation, similar to other opcodes using * 0.03125.
//
// Rounding behavior:
// Adds 0x1f (31) before shift for positive values to round up.
// Negative values shift without adjustment (round toward zero).
//
// Related opcodes:
// - 0x8F: Returns uGpffffb64c (different frame delta global)
// - 0x91: Uses (DAT_003555bc/32) for parameter ramping
//
// PS2-specific notes:
// - Frame-independent timing calculation
// - Different global from uGpffffb64c (0x8F)
// - May represent sub-frame timing or different clock source

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Frame time/delta value (different from uGpffffb64c)
extern int32_t DAT_003555bc;

// Original signature: int FUN_002612a0(void)
int32_t opcode_0x93_scale_param_by_frame_delta_and_shift(void)
{
  int32_t param;
  int32_t scaled;
  int32_t rounded;

  // Read parameter from VM
  bytecode_interpreter(&param);

  // Scale by frame delta
  scaled = param * DAT_003555bc;

  // Round positive values up before shift
  rounded = scaled + 0x1F;
  if (scaled < 0)
  {
    rounded = scaled;
  }

  // Divide by 32 (right shift 5 bits)
  return rounded >> 5;
}

// Original signature preserved for cross-reference
// int FUN_002612a0(void)
