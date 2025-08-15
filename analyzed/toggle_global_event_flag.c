// Analyzed re-expression of FUN_00266418
// Original: uint FUN_00266418(uint param_1)
// Purpose: Toggle a single global event/flag bit at index param_1.
// Returns: the bit mask if the bit is set after toggle; 0 otherwise.
// Bitmap: base DAT_00342b70, 0x900 bytes => 18432 flags [0..18431]

#include <stdint.h>

extern unsigned char DAT_00342b70[];

uint32_t toggle_global_event_flag(uint32_t bit_index)
{
  uint32_t byte_index = bit_index >> 3;
  if (byte_index > 0x8ff)
  {
    return 0;
  }
  uint8_t mask = (uint8_t)(1u << (bit_index & 7));
  uint8_t value = DAT_00342b70[byte_index] ^ mask;
  DAT_00342b70[byte_index] = value;
  return (value & mask) ? mask : 0u;
}
