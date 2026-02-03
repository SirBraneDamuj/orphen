// Opcode 0x97 — set_3d_vector_with_rgb
// Original: FUN_00261910
//
// Summary:
// - Reads 6 parameters from VM
// - Normalizes first 3 parameters as XYZ vector
// - Packs last 3 as RGB color (24-bit)
// - Stores to globals and calls graphics function
// - Returns 0
//
// Parameters:
// - param0: X component (normalized by fGpffff8cd0)
// - param1: Y component (normalized by fGpffff8cd0)
// - param2: Z component (normalized by fGpffff8cd0)
// - param3: R value (0-255, stored in bits 16-23)
// - param4: G value (0-255, stored in bits 8-15)
// - param5: B value (0-255, stored in bits 0-7)
//
// Side effects:
// - Stores normalized XYZ to DAT_003439c8/cc/d0 (floats)
// - Packs RGB as (R<<16)|(G<<8)|B to uGpffffb700
// - Calls FUN_00216510(0x3439c8) with address of vector
//
// Related opcodes:
// - 0x96: set_global_rgb_color (RGB only)
// - 0x98: set_3d_vector_polar_with_rgb (polar coordinates)
// - 0x99: set_3d_vector_simple (no RGB)
//
// PS2-specific notes:
// - Graphics/lighting parameter update
// - Vector may be light direction or normal
// - RGB likely light color or material property

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Graphics function
extern void FUN_00216510(uint32_t address);

// Normalization scale factor
extern float fGpffff8cd0;

// 3D vector storage (X, Y, Z)
extern float DAT_003439c8;
extern float DAT_003439cc;
extern float DAT_003439d0;

// RGB color storage
extern uint32_t uGpffffb700;

// Original signature: undefined8 FUN_00261910(void)
uint64_t opcode_0x97_set_3d_vector_with_rgb(void)
{
  int32_t x, y, z;
  int32_t r, g, b;

  // Read XYZ parameters
  bytecode_interpreter(&x);
  bytecode_interpreter((uint32_t)&x | 4);
  bytecode_interpreter((uint32_t)&x | 8);
  bytecode_interpreter((uint32_t)&x | 0xC);

  // Read RGB parameters
  bytecode_interpreter(&r);
  bytecode_interpreter(&g);

  // Normalize vector components
  DAT_003439c8 = (float)x / fGpffff8cd0;
  DAT_003439cc = (float)y / fGpffff8cd0;
  DAT_003439d0 = (float)z / fGpffff8cd0;

  // Pack RGB color (R<<16 | G<<8 | B)
  uGpffffb700 = (r << 16) | (g << 8) | b;

  // Submit to graphics system
  FUN_00216510(0x3439c8);

  return 0;
}

// Original signature preserved for cross-reference
// undefined8 FUN_00261910(void)
