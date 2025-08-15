// Analyzed re-expression of FUN_00266368
// Original: uint FUN_00266368(uint param_1)
// Purpose: Test a single global event/flag bit at index param_1.
// Returns: non-zero if set; 0 if clear or out-of-range.
// Bitmap: base DAT_00342b70, 0x900 bytes => 18432 flags [0..18431]

#include <stdint.h>

extern unsigned char DAT_00342b70[];

uint32_t get_global_event_flag(uint32_t bit_index)
{
  if ((bit_index >> 3) > 0x8ff)
  {
    return 0;
  }
  uint8_t byte = DAT_00342b70[bit_index >> 3];
  return (byte & (1u << (bit_index & 7))) ? 1u : 0u;
}
