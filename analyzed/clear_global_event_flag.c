// Analyzed re-expression of FUN_002663d8
// Original: void FUN_002663d8(uint param_1)
// Purpose: Clear a single global event/flag bit at index param_1.
// Bitmap: base DAT_00342b70, 0x900 bytes => 18432 flags [0..18431]

#include <stdint.h>

extern unsigned char DAT_00342b70[];

void clear_global_event_flag(uint32_t bit_index)
{
  uint32_t byte_index = bit_index >> 3;
  if (byte_index < 0x900)
  {
    DAT_00342b70[byte_index] &= (uint8_t)~(1u << (bit_index & 7));
  }
}
