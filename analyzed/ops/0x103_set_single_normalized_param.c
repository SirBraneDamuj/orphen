// Opcode 0x103 — set_single_normalized_param
// Original: FUN_00262488
//
// Summary:
// - Reads 2 parameters from VM
// - Normalizes second parameter
// - Calls graphics function with normalized value and first param
// - Returns 0
//
// Parameters:
// - param0: First value (passed directly)
// - param1: Second value (normalized by DAT_00352c64)
//
// Side effects:
// - Calls FUN_0021b480(normalized_param1, param0)
//
// Usage pattern:
// Simpler than 0x101/0x102, likely sets single scalar rendering
// parameter (alpha, scale, rotation, etc.) with mode/index in param0.
//
// Related opcodes:
// - 0x101/0x105/0x107: 2D parameter pairs
// - 0x102/0x106/0x108: Quad parameters
//
// PS2-specific notes:
// - Single normalized float suggests scalar property
// - param0 may be register/channel/mode selector
// - FUN_0021b480 likely sets rendering state

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Graphics function
extern void FUN_0021b480(float normalized_value, uint32_t param0);

// Normalization scale factor
extern float DAT_00352c64;

// Original signature: undefined8 FUN_00262488(void)
uint64_t opcode_0x103_set_single_normalized_param(void)
{
  uint32_t param0;
  int32_t param1;

  // Read parameters
  bytecode_interpreter(&param0);
  bytecode_interpreter((uint32_t)&param0 | 4);

  // Normalize and call function
  FUN_0021b480((float)param1 / DAT_00352c64, param0);

  return 0;
}

// Original signature preserved for cross-reference
// undefined8 FUN_00262488(void)
