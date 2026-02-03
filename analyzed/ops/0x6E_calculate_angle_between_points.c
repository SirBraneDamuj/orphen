// Opcode 0x6E — calculate_angle_between_points
// Original: FUN_0025fe98
//
// Summary:
// - Calculates angle between two 2D points using atan2
// - Reads 4 script parameters: (x1, y1, x2, y2) as integer coordinates
// - Normalizes coordinates by global divisor fGpffff8c7c
// - Computes angle: atan2(y1-y2, x1-x2)
// - Applies transformation via FUN_00216690 (likely angle wrapping/normalization)
// - Scales result back by divisor and submits
//
// Script Parameters (4 total):
// - param0: X coordinate of point 1 (integer)
// - param1: Y coordinate of point 1 (integer)
// - param2: X coordinate of point 2 (integer)
// - param3: Y coordinate of point 2 (integer)
//
// Processing:
// 1. Read 4 parameters via bytecode_interpreter
// 2. Normalize all coordinates: (int)value / fGpffff8c7c
// 3. Calculate deltas: dx = x1 - x2, dy = y1 - y2
// 4. Compute angle: atan2(dy, dx) via FUN_00305408
// 5. Transform angle: FUN_00216690(angle) - likely wraps to 0-360 or -π to π
// 6. Scale back: transformed_angle * fGpffff8c7c
// 7. Submit result via FUN_0030bd20 (float→int32 conversion)
//
// Algorithm Details:
// - atan2(dy, dx) returns angle in radians from X-axis to point vector
// - Angle measured from point2 toward point1
// - FUN_00216690 likely normalizes angle (e.g., wrap to 0-2π or convert to degrees)
// - Result scaled back by divisor before submission
//
// FUN_00305408: atan2f implementation
// - Standard atan2(y, x) trigonometric function
// - Returns angle in radians (-π to π typically)
// - Handles all quadrants correctly
//
// FUN_00216690: Angle transformation/wrapping
// - Takes float angle (likely in radians or degrees)
// - Returns transformed float (likely wrapped/normalized)
// - Common transformations: 0-2π wrap, -π to π, radian↔degree conversion
//
// Global Normalization Divisor:
// - fGpffff8c7c: Maps script integer coordinates to world-space floats
// - Same coordinate system as position opcodes
// - Result scaled back by same divisor (preserves units)
//
// Use Cases:
// - Calculate direction from one entity to another
// - Determine camera look-at angle
// - Compute projectile trajectory angle
// - Calculate facing direction for AI
// - Determine UI arrow/pointer rotation
//
// Typical Usage Pattern:
// ```
// // Get angle from entity A (100, 200) to entity B (300, 400)
// 0x6E <x1=100> <y1=200> <x2=300> <y2=400>
// // Result: angle in normalized units, ready for rotation commands
// ```
//
// Coordinate System:
// - 2D calculation (no Z coordinate, uses X and Y)
// - Standard atan2 quadrant handling:
//   - +X, +Y: 0 to π/2 (northeast)
//   - -X, +Y: π/2 to π (northwest)
//   - -X, -Y: -π to -π/2 (southwest)
//   - +X, -Y: -π/2 to 0 (southeast)
//
// PS2-specific notes:
// - FUN_00305408 likely uses VU0 or hardware atan2 instruction
// - FUN_00216690 may use lookup tables for fast angle normalization
// - Result submitted via FUN_0030bd20 for downstream use in VM
//
// Relationship to other opcodes:
// - Opcode 0x6E: Calculate angle between points (this opcode)
// - Opcode 0x6F: Calculate distance between points (similar param structure)
// - Opcode 0x70: submit_angle_to_target (uses entity positions)
// - Both 0x6E and 0x6F work with explicit coordinates, not entities
//
// Returns: void (result submitted via FUN_0030bd20)
//
// Original signature: void FUN_0025fe98(void)

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Global normalization divisor (coordinate space conversion)
extern float fGpffff8c7c;

// atan2f function: returns angle from X-axis to point (y, x)
// Returns angle in radians (-π to π)
extern uint32_t FUN_00305408(float y, float x);

// Angle transformation/wrapping function
// Likely wraps angle to standard range (0-2π, -π to π, or converts units)
// Returns transformed float value
extern uint32_t FUN_00216690(uint32_t angle_raw);

// Submit result to VM (float→int32 conversion and dispatch)
extern void FUN_0030bd20(float value);

// Analyzed implementation
void opcode_0x6e_calculate_angle_between_points(void)
{
  int32_t x1; // Stack offset: +0x30 - Point 1 X coordinate
  int32_t y1; // Stack offset: +0x2C - Point 1 Y coordinate
  int32_t x2; // Stack offset: +0x28 - Point 2 X coordinate
  int32_t y2; // Stack offset: +0x24 - Point 2 Y coordinate
  float divisor;
  uint32_t angle_raw;
  float angle_transformed;
  float dx, dy;

  // Read 4 coordinate parameters
  bytecode_interpreter(&x1); // Point 1 X
  divisor = fGpffff8c7c;     // Cache divisor
  bytecode_interpreter(&y1); // Point 1 Y
  bytecode_interpreter(&x2); // Point 2 X
  bytecode_interpreter(&y2); // Point 2 Y

  // Calculate angle from point2 to point1
  // dy = y1 - y2 (normalized to world space)
  // dx = x1 - x2 (normalized to world space)
  dy = ((float)y1 / divisor) - ((float)y2 / divisor);
  dx = ((float)x1 / divisor) - ((float)x2 / divisor);

  // Compute angle using atan2(dy, dx)
  // Returns angle in radians from X-axis to vector (dx, dy)
  angle_raw = FUN_00305408(dy, dx);

  // Transform/wrap angle (likely normalize to standard range)
  angle_transformed = (float)FUN_00216690(angle_raw);

  // Scale result back to script units and submit
  FUN_0030bd20(angle_transformed * divisor);

  return;
}

// Original signature wrapper
void FUN_0025fe98(void)
{
  opcode_0x6e_calculate_angle_between_points();
}
