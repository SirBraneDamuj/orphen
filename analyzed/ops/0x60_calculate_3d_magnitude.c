/*
 * Opcode 0x60 - Calculate 3D Vector Magnitude
 * Original function: FUN_0025f428
 *
 * Evaluates three expressions (X, Y, Z components), normalizes them by a divisor,
 * computes the 3D magnitude (length/distance) via sqrt(x² + y² + z²), then scales
 * and submits the result.
 *
 * Unlike 0x74 which calculates 2D XZ distance between entities, this opcode computes
 * full 3D magnitude from arbitrary expression values (not entity positions).
 *
 * BEHAVIOR:
 * 1. Evaluate three expressions: x_raw, y_raw, z_raw
 * 2. Normalize: x = x_raw / DAT_00352bd4
 *               y = y_raw / DAT_00352bd4
 *               z = z_raw / DAT_00352bd4
 * 3. Calculate magnitude: mag = FUN_00227798(x, y, z) = sqrt(x² + y² + z²)
 * 4. Scale result: result = mag * DAT_00352bd4
 * 5. Submit via FUN_0030bd20(result)
 *
 * PARAMETERS (inline):
 * - x_component (int expression) - X component of vector
 * - y_component (int expression) - Y component of vector
 * - z_component (int expression) - Z component of vector
 *
 * RETURN VALUE:
 * None (void). Result is submitted via FUN_0030bd20.
 *
 * GLOBAL READS:
 * - DAT_00352bd4: Normalization divisor for all components (likely ~100.0)
 * - DAT_70000000: Stack pointer for FUN_00227798 temporary frame (0x170 bytes)
 *
 * CALL GRAPH:
 * - FUN_0025c258: Expression evaluator (called 3 times for x, y, z)
 * - FUN_00227798: 3D vector magnitude calculation (sqrt(x² + y² + z²))
 *   - Uses VU0 or FPU hardware for vector operations
 *   - Allocates 0x170-byte stack frame at DAT_70000000
 *   - Calls FUN_00227840 for actual computation
 *   - Returns result via DAT_00354d4e
 * - FUN_0030bd20: Submit result value to expression stack/output
 *
 * USE CASES:
 * - Calculate distance from origin (0,0,0) to point (x,y,z)
 * - Compute vector length for normalization
 * - Measure 3D velocity/acceleration magnitude
 * - Distance calculations in full 3D space (vs 0x74 which is 2D XZ only)
 * - Used when script needs length of arbitrary 3D vector
 *
 * COMPARISON WITH RELATED OPCODES:
 * - 0x5E/0x5F: Calculate single polar component (cos/sin), 2 params
 * - 0x5D: Add polar velocity to entity (stateful), 3 params
 * - 0x60: Calculate 3D magnitude (stateless), 3 params
 * - 0x74: Calculate 2D XZ distance between entities, 2 params (entity selectors)
 * - 0x71: Calculate distance to target entity, 1 param (selector)
 *
 * TYPICAL SCRIPT SEQUENCES:
 *
 * Example 1: Calculate distance from origin
 *   push entity_x
 *   push entity_y
 *   push entity_z
 *   0x60              # distance = sqrt(x² + y² + z²)
 *   compare threshold
 *   jump_if_greater
 *
 * Example 2: Normalize velocity vector
 *   push vel_x
 *   push vel_y
 *   push vel_z
 *   0x60              # magnitude = length of velocity
 *   store mag
 *   push vel_x
 *   divide mag
 *   store norm_x      # normalized component
 *
 * Example 3: Check if point within sphere
 *   push (px - center_x)
 *   push (py - center_y)
 *   push (pz - center_z)
 *   0x60              # distance from center
 *   push radius
 *   compare_less_than # inside sphere?
 *
 * NOTES:
 * - Normalization and scaling by same divisor (DAT_00352bd4) means they cancel out
 *   in the final formula: (x/d, y/d, z/d) → sqrt(...) * d = sqrt(x² + y² + z²)
 * - This pattern allows integer input values to be interpreted as fixed-point
 * - FUN_00227798 uses VU0 hardware for fast 3D vector operations (PS2-specific)
 * - Stack frame allocation at DAT_70000000 suggests separate computation context
 * - Unlike entity-based opcodes, this works with arbitrary expression values
 */

extern float DAT_00352bd4; // Normalization divisor (~100.0)
extern int DAT_70000000;   // Stack pointer for vector operations
extern short DAT_00354d4e; // Result storage from FUN_00227798

extern void FUN_0025c258(int *out_result);            // Expression evaluator
extern float FUN_00227798(float x, float y, float z); // 3D magnitude: sqrt(x²+y²+z²)
extern void FUN_0030bd20(float value);                // Submit result value

void calculate_3d_magnitude(void) // orig: FUN_0025f428
{
  float divisor;
  int x_raw;
  int y_raw;
  int z_raw;
  float x_norm;
  float y_norm;
  float z_norm;
  float magnitude;
  float result;

  // Evaluate three expressions from bytecode stream
  FUN_0025c258(&x_raw); // First parameter: X component

  // Save divisor (used for all normalizations and final scaling)
  divisor = DAT_00352bd4;

  FUN_0025c258(&y_raw); // Second parameter: Y component
  FUN_0025c258(&z_raw); // Third parameter: Z component

  // Normalize all components by divisor
  // This converts integer input values to floating-point world units
  x_norm = (float)x_raw / divisor;
  y_norm = (float)y_raw / divisor;
  z_norm = (float)z_raw / divisor;

  // Calculate 3D magnitude using VU0/FPU hardware
  // FUN_00227798 allocates temp frame at DAT_70000000 (+0x170 bytes)
  // Computes: sqrt(x² + y² + z²)
  // Returns result via DAT_00354d4e
  magnitude = FUN_00227798(x_norm, y_norm, z_norm);

  // Scale result back up by same divisor
  // Note: Since we divided by divisor then multiplied back, the divisor
  // effectively cancels out, but this pattern allows integer input values
  // to be interpreted as fixed-point (e.g., 100 = 1.0 units)
  result = magnitude * divisor;

  // Submit result to expression stack/output
  FUN_0030bd20(result);

  return;
}
