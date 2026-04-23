// Opcode 0x101/0x105/0x107 — set_2d_params_normalized
// Original: FUN_00262118
//
// Summary:
// - Reads 3 parameters from VM
// - Normalizes two parameters by scale factor
// - Calls different functions based on opcode ID
// - Returns 0
//
// Parameters:
// - param0: First value (passed directly)
// - param1: Second value (normalized)
// - param2: Third value (normalized)
//
// Opcode branching:
// - 0x101: Normalizes by DAT_00352c4c, calls FUN_0021abc8
// - 0x105: Normalizes by DAT_00352c50, calls FUN_0021d410
// - 0x107: Normalizes by DAT_00352c54, calls FUN_0021c710
//
// Side effects:
// - Calls graphics/rendering function with normalized float pair and raw param0
//
// Normalization pattern:
// All three opcodes normalize param1 and param2 to floats, likely
// representing 2D coordinates, UV texture mapping, or screen positions.
//
// Related opcodes:
// - 0x102/0x106/0x108: Similar pattern with 8 params instead of 3
//
// PS2-specific notes:
// - Different scale factors suggest different coordinate spaces
// - param0 may be mode/flags/index parameter
// - Functions likely set rendering state or texture parameters

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Graphics functions for each opcode
extern void FUN_0021abc8(float norm1, float norm2, uint32_t param0); // 0x101
extern void FUN_0021d410(float norm1, float norm2, uint32_t param0); // 0x105
extern void FUN_0021c710(float norm1, float norm2, uint32_t param0); // 0x107

// Normalization scale factors
extern float DAT_00352c4c; // 0x101
extern float DAT_00352c50; // 0x105
extern float DAT_00352c54; // 0x107

// Opcode ID (set by interpreter)
extern int16_t DAT_00355cd8;

// Original signature: undefined8 FUN_00262118(void)
uint64_t opcode_0x101_0x105_0x107_set_2d_params_normalized(void)
{
  uint32_t param0;
  int32_t param1, param2;
  int16_t opcode;

  opcode = DAT_00355cd8;

  // Read parameters
  bytecode_interpreter(&param0);
  bytecode_interpreter((uint32_t)&param0 | 4);
  bytecode_interpreter((uint32_t)&param0 | 8);

  // Branch based on opcode ID
  if (opcode == 0x105)
  {
    FUN_0021d410((float)param1 / DAT_00352c50, (float)param2 / DAT_00352c50, param0);
  }
  else if (opcode < 0x106)
  {
    if (opcode == 0x101)
    {
      FUN_0021abc8((float)param1 / DAT_00352c4c, (float)param2 / DAT_00352c4c, param0);
    }
  }
  else if (opcode == 0x107)
  {
    FUN_0021c710((float)param1 / DAT_00352c54, (float)param2 / DAT_00352c54, param0);
  }

  return 0;
}

// Original signature preserved for cross-reference
// undefined8 FUN_00262118(void)
