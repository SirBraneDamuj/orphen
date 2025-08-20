/*
 * Float → int32 (truncate toward zero, saturate on overflow)
 * Original: FUN_0030bd20
 *
 * Summary
 * - Interprets the input as an IEEE-754 single-precision float and converts it to a 32-bit signed
 *   integer with these rules:
 *   - NaN, subnormals, and +/-0 → 0
 *   - |x| < 1.0 → 0
 *   - Normal finite values: truncate toward zero
 *   - |x| >= 2^31 or +/-Inf → clamp to INT_MAX/INT_MIN
 *
 * How it works in the original:
 * - Calls FUN_0030b428 to decompose the float bits into {class, sign, exponent, mantissa}:
 *   class codes: 0/1: NaN, 2: zero/subnormal, 3: normal, 4: infinity
 *   sign: 0 or 1, exponent: unbiased, mantissa: normalized with implicit 1<<30 OR’d in
 * - For normal numbers, it right-shifts the normalized mantissa by (30 - exponent) when exponent<31
 *   to produce the integer magnitude; then applies sign.
 * - For exponent>=31 or infinity, it saturates to 0x7fffffff or 0x80000000 based on sign.
 *
 * Context
 * - This routine is used all over the VM/opcode handlers as the final “submit” step after computing
 *   a float parameter (e.g., 0x70/0x71/0x72 call this), converting to the engine’s 32-bit int domain.
 *
 * Original signature (for reference):
 *   // uint FUN_0030bd20(undefined4 param_1)
 *
 * PS2/IEEE-754 notes
 * - Matches the standard single-precision layout; behavior aligns with floor toward zero (trunc) and
 *   saturation for overflow or infinities.
 */

#include <stdint.h>
#include <limits.h>

// Helper: reinterpret float bits (portable via union)
static inline uint32_t bits_of_float(float x)
{
  union
  {
    float f;
    uint32_t u;
  } v = {.f = x};
  return v.u;
}

// Analyzed, readable version of FUN_0030bd20 behavior
int32_t float_to_int32_saturating(float x)
{
  uint32_t u = bits_of_float(x);
  uint32_t frac = u & 0x7FFFFF;      // raw mantissa
  int exp = (int)((u >> 23) & 0xFF); // raw biased exponent
  int sgn = (int)(u >> 31);          // 0 or 1

  // Classify: NaN or zero/subnormal → 0; Infinity handled later
  if (exp == 0)
  {
    return 0; // zero or subnormal → 0
  }
  if (exp == 0xFF)
  {
    // Infinity or NaN: NaN → 0 (matches original), Infinity → saturate
    if (frac == 0)
    {
      return sgn ? INT_MIN : INT_MAX; // +/-Inf → clamp
    }
    return 0; // NaN → 0
  }

  // Normalized number
  int e = exp - 127; // unbiased exponent
  if (e < 0)
  {
    return 0; // |x| < 1.0 → 0
  }
  if (e >= 31)
  {
    return sgn ? INT_MIN : INT_MAX; // overflow → saturate
  }

  // Build mantissa with implicit leading 1 at bit 23; shift to have leading 1 at bit 30
  uint32_t mant = (frac << 7) | 0x40000000u; // 1.xxx in Q1.30
  // Position integer by shifting right by (30 - e); this truncates toward zero
  uint32_t mag = mant >> (uint32_t)(30 - e);
  int32_t val = (sgn == 0) ? (int32_t)mag : -(int32_t)mag;
  return val;
}

// Optional thin wrapper named after the original for easier xref in analysis code
// Note: The raw function treats its argument as float bits; C will pass a float here directly.
uint32_t analyzed_FUN_0030bd20(float x)
{
  return (uint32_t)float_to_int32_saturating(x);
}
