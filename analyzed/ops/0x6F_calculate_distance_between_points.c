// Opcode 0x6F — calculate_distance_between_points
// Original: FUN_0025ff58
//
// Summary:
// - Calculates 2D Euclidean distance between two points
// - Reads 4 script parameters: (x1, y1, x2, y2) as integer coordinates
// - Normalizes coordinates by global divisor fGpffff8c80
// - Computes distance: sqrt((x1-x2)² + (y1-y2)²)
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
// 2. Normalize all coordinates: (int)value / fGpffff8c80
// 3. Calculate deltas: dx = x1 - x2, dy = y1 - y2
// 4. Compute distance: sqrt(dx² + dy²) via SQRT macro
// 5. Scale back: distance * fGpffff8c80
// 6. Submit result via FUN_0030bd20 (float→int32 conversion)
//
// Algorithm Details:
// - Standard Pythagorean distance formula in 2D
// - dx = x1 - x2 (horizontal delta)
// - dy = y1 - y2 (vertical delta)
// - distance = sqrt(dx² + dy²)
// - Result is always positive (distance magnitude)
//
// SQRT Implementation:
// - Likely hardware sqrt instruction (PS2 VU0 or FPU)
// - Takes float input, returns float output
// - Standard square root operation
//
// Global Normalization Divisor:
// - fGpffff8c80: Maps script integer coordinates to world-space floats
// - Same coordinate system as position opcodes
// - Result scaled back by same divisor (preserves units)
//
// Use Cases:
// - Calculate distance between two entities
// - Proximity detection (is entity within range?)
// - Pathfinding distance calculations
// - Range checks for attacks/abilities
// - Trigger radius detection
// - Audio volume attenuation by distance
//
// Typical Usage Pattern:
// ```
// // Calculate distance from entity A (100, 200) to entity B (300, 400)
// 0x6F <x1=100> <y1=200> <x2=300> <y2=400>
// // Result: distance in normalized units
//
// // Example: Check if distance < threshold
// 0x6F <x1> <y1> <x2> <y2>  // Calculate distance
// push <threshold>          // Load threshold value
// compare_lt                // Check if distance < threshold
// if_true <do_action>       // Branch if within range
// ```
//
// Coordinate System:
// - 2D calculation (no Z coordinate, uses X and Y)
// - Horizontal plane distance only
// - For 3D distance, see opcode 0x60 (calculate_3d_magnitude)
//
// Comparison with Similar Opcodes:
// - Opcode 0x6E: Calculate angle between points (atan2)
// - Opcode 0x6F: Calculate distance between points (this opcode)
// - Opcode 0x60: Calculate 3D magnitude (includes Z coordinate)
// - Opcode 0x71: submit_distance_to_target (uses entity positions)
//
// PS2-specific notes:
// - SQRT likely uses VU0 hardware sqrt or FRSQRT approximation
// - Fast distance calculations critical for real-time AI/physics
// - Result submitted via FUN_0030bd20 for downstream VM use
//
// Optimization Note:
// - For distance comparisons, consider using squared distance:
//   dx² + dy² (skips expensive sqrt operation)
// - This opcode always computes full sqrt for actual distance value
//
// Returns: void (result submitted via FUN_0030bd20)
//
// Original signature: void FUN_0025ff58(void)

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Global normalization divisor (coordinate space conversion)
extern float fGpffff8c80;

// Submit result to VM (float→int32 conversion and dispatch)
extern void FUN_0030bd20(float value);

// Analyzed implementation
void opcode_0x6f_calculate_distance_between_points(void)
{
  int32_t x1; // Stack offset: +0x30 - Point 1 X coordinate
  int32_t y1; // Stack offset: +0x2C - Point 1 Y coordinate
  int32_t x2; // Stack offset: +0x28 - Point 2 X coordinate
  int32_t y2; // Stack offset: +0x24 - Point 2 Y coordinate
  float divisor;
  float dx, dy;
  float distance;

  // Read 4 coordinate parameters
  bytecode_interpreter(&x1); // Point 1 X
  divisor = fGpffff8c80;     // Cache divisor
  bytecode_interpreter(&y1); // Point 1 Y
  bytecode_interpreter(&x2); // Point 2 X
  bytecode_interpreter(&y2); // Point 2 Y

  // Calculate deltas in world space
  // dx = x1 - x2 (normalized to world units)
  // dy = y1 - y2 (normalized to world units)
  dx = ((float)x1 / divisor) - ((float)x2 / divisor);
  dy = ((float)y1 / divisor) - ((float)y2 / divisor);

  // Compute 2D Euclidean distance: sqrt(dx² + dy²)
  // SQRT is likely hardware instruction (VU0 or FPU)
  distance = SQRT(dy * dy + dx * dx);

  // Scale result back to script units and submit
  // Preserves original coordinate system units
  FUN_0030bd20(distance * divisor);

  return;
}

// Original signature wrapper
void FUN_0025ff58(void)
{
  opcode_0x6f_calculate_distance_between_points();
}
