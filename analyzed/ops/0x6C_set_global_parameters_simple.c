// Opcode 0x6C — set_global_parameters_simple
// Original: FUN_0025fca0
//
// Summary:
// - Simplified version of opcode 0x6A (set_global_normalized_parameters)
// - Reads 2 script parameters (instead of 3)
// - Applies same sin(2*x)/sin(2.0) transformation to first parameter
// - Stores results to same globals: uGpffffb6e8 and fGpffffb6ec
// - Does NOT set uGpffffbd7c or fGpffffbd80 (target/rate for interpolation)
//
// Script Parameters (2 total):
// - param0: Integer value (transformed via FUN_00218230, stored to uGpffffb6e8)
// - param1: Integer value (normalized to float, stored to fGpffffb6ec)
//
// Processing:
// - Evaluates 2 parameters using bytecode_interpreter
// - Stack layout: params at offsets +0x30, +0x2C
// - Both values normalized by fGpffff8c78 divisor
// - Param0: normalized then transformed via FUN_00218230 → uGpffffb6e8
// - Param1: normalized directly → fGpffffb6ec
//
// Global Variables Modified:
// - uGpffffb6e8: Transformed param0 (via FUN_00218230, sine-normalized)
// - fGpffffb6ec: Normalized param1 (direct float)
//
// Global Normalization Divisor:
// - fGpffff8c78: Maps script integer values to normalized float range
// - Different divisor than 0x6A which uses fGpffff8c74
//
// Comparison with Opcode 0x6A:
// - Opcode 0x6A: Sets 4 globals (target, rate, current=1.0, param2)
//   - Params: 3 values
//   - Divisor: fGpffff8c74
//   - Used with 0x6B for interpolation
// - Opcode 0x6C: Sets 2 globals (current, param1) - THIS OPCODE
//   - Params: 2 values
//   - Divisor: fGpffff8c78
//   - Direct value setting, no interpolation setup
//
// FUN_00218230 Behavior:
// - Calculates: sin(param * 2.0) / sin(2.0)
// - Returns normalized sine value (same as used in 0x6A)
// - For input 1.0: sin(2.0)/sin(2.0) = 1.0
// - For input 0.0: sin(0.0)/sin(2.0) = 0.0
//
// Usage Pattern:
// - Sets rendering/transformation parameters directly
// - Unlike 0x6A+0x6B, no interpolation/stepping involved
// - Immediate value assignment rather than gradual transition
// - Simpler interface when animation/fading not needed
//
// Typical Usage:
// ```
// 0x6C <value=100> <param=50>  // Set parameters immediately
// // uGpffffb6e8 and fGpffffb6ec updated instantly
// // No need for 0x6B stepping loop
// ```
//
// PS2-specific notes:
// - FUN_00218230 is likely hardware sine function (VU0 or FSIN)
// - Storing to uint vs float suggests type punning (IEEE-754 bit pattern)
// - uGpffffb6e8 stored as uint32 but contains float bit pattern
//
// Relationship to other opcodes:
// - Opcode 0x6A: Full parameter setup (4 globals, interpolation support)
// - Opcode 0x6B: Steps interpolation (works with 0x6A globals)
// - Opcode 0x6C: Direct parameter set (2 globals, no interpolation) - THIS OPCODE
// - All three share the same destination globals (uGpffffb6e8, fGpffffb6ec)
//
// Returns: 0 (always)
//
// Original signature: undefined8 FUN_0025fca0(void)

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Global normalization divisor (maps script integers to float range)
extern float fGpffff8c78;

// Global destination variables (shared with opcodes 0x6A and 0x6B)
extern uint32_t uGpffffb6e8; // Transformed param0 (sine-normalized)
extern float fGpffffb6ec;    // Direct normalized param1

// Sine transformation function: sin(2*x) / sin(2.0)
// Returns normalized sine value in range based on input
extern uint32_t FUN_00218230(float normalized_value);

// Analyzed implementation
uint64_t opcode_0x6c_set_global_parameters_simple(void)
{
  int32_t param0; // Stack offset: +0x30
  int32_t param1; // Stack offset: +0x2C
  float divisor;

  // Read 2 script parameters
  bytecode_interpreter(&param0); // Param 0: transformed value
  divisor = fGpffff8c78;         // Cache divisor for normalization
  bytecode_interpreter(&param1); // Param 1: linear value

  // Normalize param0 and apply sine transformation
  // Result stored to uGpffffb6e8 (float bits in uint32)
  uGpffffb6e8 = FUN_00218230((float)param0 / divisor);

  // Normalize param1 directly to float
  fGpffffb6ec = (float)param1 / divisor;

  return 0; // Success
}

// Original signature wrapper
uint64_t FUN_0025fca0(void)
{
  return opcode_0x6c_set_global_parameters_simple();
}
