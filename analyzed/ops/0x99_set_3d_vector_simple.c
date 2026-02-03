// Opcode 0x99 — set_3d_vector_simple
// Original: FUN_00261af0
//
// Summary:
// - Reads 3 parameters from VM
// - Normalizes as XYZ vector
// - Stores to globals (no RGB, no function call)
// - Returns 0
//
// Parameters:
// - param0: X component (normalized by fGpffff8cd8)
// - param1: Y component (normalized by fGpffff8cd8)
// - param2: Z component (normalized by fGpffff8cd8)
//
// Side effects:
// - Stores normalized XYZ to DAT_00343a08/0c/10 (floats)
// - Different storage address than 0x97/0x98 (0x343a08 vs 0x3439c8)
// - No RGB packing
// - No graphics function call
//
// Related opcodes:
// - 0x97: set_3d_vector_with_rgb (includes RGB, calls graphics)
// - 0x98: set_3d_vector_polar_with_rgb (polar input, includes RGB)
//
// Usage pattern:
// Simpler than 0x97/0x98, likely sets secondary vector parameter
// (camera target, offset, velocity, etc.) without color component.
//
// PS2-specific notes:
// - Storage at different address suggests different purpose
// - May be staging area for subsequent operation
// - Separate from main vector at 0x3439c8

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Normalization scale factor
extern float fGpffff8cd8;

// 3D vector storage (X, Y, Z) - different from 0x97/0x98
extern float DAT_00343a08;
extern float DAT_00343a0c;
extern float DAT_00343a10;

// Original signature: undefined8 FUN_00261af0(void)
uint64_t opcode_0x99_set_3d_vector_simple(void)
{
  int32_t x, y, z;

  // Read XYZ parameters
  bytecode_interpreter(&x);
  bytecode_interpreter((uint32_t)&x | 4);
  bytecode_interpreter((uint32_t)&x | 8);

  // Normalize and store vector components
  DAT_00343a08 = (float)x / fGpffff8cd8;
  DAT_00343a0c = (float)y / fGpffff8cd8;
  DAT_00343a10 = (float)z / fGpffff8cd8;

  return 0;
}

// Original signature preserved for cross-reference
// undefined8 FUN_00261af0(void)
