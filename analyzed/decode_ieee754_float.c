#include <stdint.h>

// PS2 type definitions
typedef unsigned int uint;
typedef unsigned char undefined4;

/**
 * IEEE 754 floating-point decoder
 *
 * This function breaks down a 32-bit IEEE 754 floating-point number into its components:
 * - Sign bit
 * - Exponent
 * - Mantissa/significand
 * - Special cases (zero, infinity, NaN)
 *
 * This is used by the graphics system for floating-point to fixed-point conversion,
 * which is essential for PS2 graphics programming where many operations use
 * fixed-point arithmetic for performance.
 *
 * Original function: FUN_0030b428
 * Address: 0x0030b428
 *
 * @param float_input Pointer to 32-bit float value to decode
 * @param result Pointer to result structure containing decoded components
 */
void decode_ieee754_float(uint *float_input, undefined4 *result)
{
  uint raw_bits;
  uint mantissa;
  uint exponent;

  raw_bits = *float_input;
  mantissa = raw_bits & 0x7fffff;       // Extract 23-bit mantissa
  exponent = (raw_bits >> 0x17) & 0xff; // Extract 8-bit exponent
  result[1] = raw_bits >> 0x1f;         // Extract sign bit

  // Handle special case: zero or denormalized numbers
  if (exponent == 0)
  {
    *result = 2; // Type: zero/denormalized
    return;
  }

  // Handle special case: infinity or NaN
  if (exponent == 0xff)
  {
    if (mantissa == 0)
    {
      *result = 4; // Type: infinity
      return;
    }

    // NaN case - check if signaling or quiet
    if ((raw_bits & 0x100000) == 0)
    {
      *result = 0; // Type: signaling NaN
    }
    else
    {
      *result = 1; // Type: quiet NaN
    }
    result[3] = mantissa;
    return;
  }

  // Normal floating-point number
  result[3] = (mantissa << 7) | 0x40000000; // Normalized mantissa with implicit leading 1
  result[2] = exponent - 0x7f;              // Biased exponent (subtract 127)
  *result = 3;                              // Type: normal number
  return;
}

// Result structure layout:
// result[0] = type (0=sNaN, 1=qNaN, 2=zero/denorm, 3=normal, 4=infinity)
// result[1] = sign bit (0=positive, 1=negative)
// result[2] = unbiased exponent (for normal numbers)
// result[3] = normalized mantissa (for normal numbers) or raw mantissa (for NaN)
