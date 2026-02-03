// Opcode 0x95 — get_absolute_value
// Original: FUN_00261890
//
// Summary:
// - Calls FUN_00216868() to get a value
// - Returns absolute value of result
// - No VM parameters read
//
// Return value:
// - |FUN_00216868()|
//
// Function behavior:
// FUN_00216868() likely reads from a register, state variable,
// or performs a calculation. This opcode wraps it to ensure
// the returned value is always non-negative.
//
// Usage pattern:
// Common for distance/magnitude calculations where direction
// doesn't matter, or when checking deviation from a reference point.
//
// Related opcodes:
// - 0x60: calculate_3d_magnitude (sqrt-based absolute distance)
// - 0x6F: calculate_distance_between_points
// - 0x74: calculate_entity_distance
//
// PS2-specific notes:
// - Simple wrapper for absolute value operation
// - FUN_00216868 may read hardware register or compute value
// - No parameters suggests reading from global state

#include <stdint.h>

// Function that returns a signed value
extern int32_t FUN_00216868(void);

// Original signature: int FUN_00261890(void)
int32_t opcode_0x95_get_absolute_value(void)
{
  int32_t value;

  // Get value from function
  value = FUN_00216868();

  // Return absolute value
  if (value < 0)
  {
    value = -value;
  }

  return value;
}

// Original signature preserved for cross-reference
// int FUN_00261890(void)
