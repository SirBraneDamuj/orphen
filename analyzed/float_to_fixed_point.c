#include <stdint.h>

// PS2 type definitions
typedef unsigned int uint;
typedef unsigned char undefined4;
typedef int int32_t;

// Forward declaration
extern void decode_ieee754_float(uint *float_input, undefined4 *result);

/**
 * Float to fixed-point converter
 *
 * Converts a 32-bit IEEE 754 floating-point number to a 32-bit fixed-point number.
 * This is commonly used in PS2 graphics programming where fixed-point arithmetic
 * is preferred over floating-point for performance reasons.
 *
 * The function handles special cases like zero, infinity, and NaN, and performs
 * proper range clamping to prevent overflow in fixed-point representation.
 *
 * This function is used extensively in the graphics pipeline, particularly for
 * converting texture coordinates and vertex positions from floating-point to
 * the fixed-point format expected by the PS2 GPU.
 *
 * Original function: FUN_0030bd20
 * Address: 0x0030bd20
 *
 * @param float_value The floating-point value to convert (passed as undefined4)
 * @return Fixed-point representation of the input value
 */
uint float_to_fixed_point(undefined4 float_value)
{
  uint result;
  uint float_type;
  int32_t is_negative;
  int32_t exponent;
  uint mantissa;
  undefined4 float_components[4];

  // Store input and decode IEEE 754 components
  float_components[0] = float_value;
  decode_ieee754_float((uint *)float_components, &float_type);

  // Handle zero and denormalized numbers
  if ((float_type == 2) || (float_type < 2))
  {
    return 0;
  }

  // Handle infinity
  if (float_type == 4)
  {
    // Return maximum positive or negative fixed-point value
    if (is_negative == 0)
    {
      return 0x7fffffff; // Maximum positive
    }
    else
    {
      return 0x80000000; // Maximum negative (minimum value)
    }
  }

  // Handle normal floating-point numbers
  if (float_type == 3)
  {
    // Check for negative exponent (number too small for fixed-point)
    if (exponent < 0)
    {
      return 0;
    }

    // Convert mantissa to fixed-point if exponent is within range
    if (exponent < 0x1f) // 31 bits maximum
    {
      // Shift mantissa based on exponent to create fixed-point value
      mantissa = mantissa >> ((0x1e - exponent) & 0x1f);

      // Apply sign
      if (is_negative == 0)
      {
        return mantissa; // Positive result
      }
      return -mantissa; // Negative result (two's complement)
    }
  }

  // Handle overflow case - return saturated values
  result = 0x7fffffff; // Maximum positive fixed-point
  if (is_negative != 0)
  {
    result = 0x80000000; // Maximum negative fixed-point
  }

  return result;
}

/**
 * Note: This function expects the fixed-point format to have the decimal point
 * positioned based on the calling context. In graphics applications, this is
 * often used for coordinates scaled by factors like 4096.0, suggesting a
 * 20.12 fixed-point format (20 integer bits, 12 fractional bits).
 */
