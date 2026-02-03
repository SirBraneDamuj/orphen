// Opcode 0x6A — set_global_normalized_parameters
// Original: FUN_0025fb80
//
// Summary:
// - Reads 3 script parameters (integer coordinates/values)
// - Normalizes all values by global divisor fGpffff8c74
// - Applies sin(2*x)/sin(2.0) transformation to 2 values via FUN_00218230
// - Stores results to 4 global variables (uGpffffbd7c, fGpffffbd80, uGpffffb6e8, fGpffffb6ec)
// - Does NOT operate on entities - writes directly to global state
//
// Script Parameters (3 total):
// - param0: Integer value (transformed via FUN_00218230, stored to uGpffffbd7c)
// - param1: Integer value (normalized to float, stored to fGpffffbd80)
// - param2: Integer value (normalized to float, stored to fGpffffb6ec)
//
// Processing:
// - Evaluates 3 parameters using bytecode_interpreter
// - Stack layout: params at offsets +0x30, +0x2C, +0x28
// - All values normalized by fGpffff8c74 divisor
// - Param0: normalized then transformed via FUN_00218230 → uGpffffbd7c
// - Param1: normalized directly → fGpffffbd80
// - Param2: normalized directly → fGpffffb6ec
// - Constant: FUN_00218230(1.0) → uGpffffb6e8
//
// FUN_00218230 Behavior (from source analysis):
// - Calculates: sin(param * 2.0) / sin(2.0)
// - Calls FUN_00305670 (sine function) twice
// - FUN_00305670(param + param) = sin(2*param)
// - FUN_00305670(0x40000000) = sin(2.0) = 0.909297426825681
// - Returns ratio of sin(2*param) / sin(2.0)
// - This is a normalized sine wave transformation
// - For input 1.0: sin(2.0)/sin(2.0) = 1.0
// - For input 0.0: sin(0.0)/sin(2.0) = 0.0
//
// Global Variables Modified:
// - uGpffffbd7c: Transformed param0 (via FUN_00218230)
// - fGpffffbd80: Normalized param1 (direct float)
// - uGpffffb6e8: Constant transformed 1.0 (always sin(2.0)/sin(2.0) = 1.0)
// - fGpffffb6ec: Normalized param2 (direct float)
//
// Global Normalization Divisor:
// - fGpffff8c74: Maps script integer values to normalized float range
// - Same pattern as position normalization in other opcodes
// - Likely converts fixed-point or integer coordinates to world units
//
// Usage Pattern Analysis:
// - These globals appear to control rendering or transformation parameters
// - The sine transformation suggests angular/wave-based interpolation
// - Constant uGpffffb6e8 = 1.0 suggests it's a scale/reference value
// - Pattern: (transformed, linear, constant, linear) suggests different param roles
//
// PS2-specific notes:
// - FUN_00305670 is likely hardware sine function (VU0 or FSIN instruction)
// - The division by sin(2.0) normalizes the sine range to a specific scale
// - Storing to uint vs float suggests type punning (IEEE-754 bit pattern storage)
// - uGpffffbd7c and uGpffffb6e8 stored as uint32 but contain float bit patterns
//
// Relationship to other opcodes:
// - Unlike opcodes 0x67-0x69, this doesn't operate on entities
// - Writes to global state variables used by other systems
// - The normalized parameters may control camera, lighting, or rendering
// - The sine transformation suggests smooth interpolation/easing
//
// Related Global Variable Usage:
// - Search codebase shows these globals used in rendering/graphics systems
// - uGpffffb6e8 appears in entity rendering code (likely scale factor)
// - Pattern suggests these form a 4-value transformation matrix or vector
//
// Returns: 0 (always)
//
// Original signature: undefined8 FUN_0025fb80(void)

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Global normalization divisor (maps script integers to float range)
extern float fGpffff8c74;

// Global destination variables (4 total)
extern uint32_t uGpffffbd7c; // Transformed param0 (sine-normalized)
extern float fGpffffbd80;    // Direct normalized param1
extern uint32_t uGpffffb6e8; // Constant 1.0 (sine-normalized)
extern float fGpffffb6ec;    // Direct normalized param2

// Sine transformation function: sin(2*x) / sin(2.0)
// Returns normalized sine value in range based on input
extern uint32_t FUN_00218230(float normalized_value);

// Analyzed implementation
uint64_t opcode_0x6a_set_global_normalized_parameters(void)
{
  int32_t param0; // Stack offset: +0x30
  int32_t param1; // Stack offset: +0x2C
  int32_t param2; // Stack offset: +0x28
  float divisor;

  // Read 3 script parameters
  bytecode_interpreter(&param0); // Param 0: transformed value
  divisor = fGpffff8c74;         // Cache divisor for normalization
  bytecode_interpreter(&param1); // Param 1: linear value
  bytecode_interpreter(&param2); // Param 2: linear value

  // Normalize param0 and apply sine transformation
  // Result stored to uGpffffbd7c (float bits in uint32)
  uGpffffbd7c = FUN_00218230((float)param0 / divisor);

  // Normalize param1 directly to float
  fGpffffbd80 = (float)param1 / divisor;

  // Store constant reference value (always 1.0 after transformation)
  // sin(2.0) / sin(2.0) = 1.0
  // IEEE-754: 0x3f800000 = 1.0f
  uGpffffb6e8 = FUN_00218230(1.0f); // Always equals 1.0 after transformation

  // Normalize param2 directly to float
  fGpffffb6ec = (float)param2 / divisor;

  return 0; // Success
}

// Original signature wrapper
uint64_t FUN_0025fb80(void)
{
  return opcode_0x6a_set_global_normalized_parameters();
}
