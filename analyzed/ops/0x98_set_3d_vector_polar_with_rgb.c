// Opcode 0x98 — set_3d_vector_polar_with_rgb
// Original: FUN_002619e0
//
// Summary:
// - Reads 6 parameters from VM
// - Converts polar coordinates (angle, magnitude, z) to cartesian (x, y, z)
// - Packs RGB color (24-bit)
// - Stores to globals and calls graphics function
// - Returns 0
//
// Parameters:
// - param0: Angle (normalized by fGpffff8cd4)
// - param1: Magnitude/radius (normalized by fGpffff8cd4)
// - param2: Z component (normalized by fGpffff8cd4)
// - param3: R value (0-255, stored in bits 16-23)
// - param4: G value (0-255, stored in bits 8-15)
// - param5: B value (0-255, stored in bits 0-7)
//
// Polar conversion:
// - X = magnitude * cos(angle)
// - Y = magnitude * sin(angle)
// - Z = z (direct)
//
// Side effects:
// - Stores converted XYZ to DAT_003439c8/cc/d0 (floats)
// - Packs RGB as (R<<16)|(G<<8)|B to uGpffffb700
// - Calls FUN_00216510(0x3439c8) with address of vector
//
// Related opcodes:
// - 0x97: set_3d_vector_with_rgb (cartesian coordinates)
// - 0x99: set_3d_vector_simple (no RGB)
//
// PS2-specific notes:
// - FUN_00305130 is cosine function
// - FUN_00305218 is sine function
// - Polar input convenient for rotational parameters

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Trigonometric functions
extern float FUN_00305130(float angle); // cos
extern float FUN_00305218(float angle); // sin

// Graphics function
extern void FUN_00216510(uint32_t address);

// Normalization scale factor
extern float fGpffff8cd4;

// 3D vector storage (X, Y, Z)
extern float DAT_003439c8;
extern float DAT_003439cc;
extern float DAT_003439d0;

// RGB color storage
extern uint32_t uGpffffb700;

// Original signature: undefined8 FUN_002619e0(void)
uint64_t opcode_0x98_set_3d_vector_polar_with_rgb(void)
{
  int32_t angle, magnitude, z;
  int32_t r, g, b;
  float norm_angle, norm_magnitude, norm_z;
  float cos_angle, sin_angle;

  // Read polar parameters
  bytecode_interpreter(&angle);
  bytecode_interpreter((uint32_t)&angle | 4);
  bytecode_interpreter((uint32_t)&angle | 8);
  bytecode_interpreter((uint32_t)&angle | 0xC);

  // Read RGB parameters
  bytecode_interpreter(&r);
  bytecode_interpreter(&g);

  // Normalize polar components
  norm_angle = (float)angle / fGpffff8cd4;
  norm_magnitude = (float)magnitude / fGpffff8cd4;
  norm_z = (float)z / fGpffff8cd4;

  // Pack RGB color (R<<16 | G<<8 | B)
  uGpffffb700 = (r << 16) | (g << 8) | b;

  // Convert polar to cartesian
  cos_angle = FUN_00305130(norm_angle);
  sin_angle = FUN_00305218(norm_angle);

  DAT_003439c8 = norm_magnitude * cos_angle; // X
  DAT_003439cc = norm_magnitude * sin_angle; // Y
  DAT_003439d0 = norm_z;                     // Z

  // Submit to graphics system
  FUN_00216510(0x3439c8);

  return 0;
}

// Original signature preserved for cross-reference
// undefined8 FUN_002619e0(void)
