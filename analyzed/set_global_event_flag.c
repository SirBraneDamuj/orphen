// Analyzed re-expression of FUN_002663a0
// Original signature: void FUN_002663a0(uint param_1)
//
// Purpose:
// - Set a single bit in a global byte-addressed bitmap starting at DAT_00342b70.
// - The bit index is 'param_1'. Bits are packed little-endian within each byte.
//
// Behavior:
// - Computes byte_index = (param_1 >> 3). If byte_index < 0x900 (bounds check),
//   sets: DAT_00342b70[byte_index] |= (1 << (param_1 & 7)).
// - Out-of-range indices are ignored silently.
//
// Bitmap size:
// - 0x900 bytes (2304 bytes) -> 18432 addressable flags [0..18431].
//
// Context:
// - Called after registering a scene script (see FUN_0025d380 analyzed as
//   register_scene_script_from_resource), e.g., with bit index 0x510.
//   Likely used as a deferred event/dirty-flag mechanism polled elsewhere.
//
// Notes:
// - Keep DAT_00342b70 as an unresolved global until the consumer(s) of this bitmap are analyzed.
// - No synchronization is evident in the caller; assumed single-threaded EE context.

#include <stdint.h>

// Unresolved global bitmap base
extern unsigned char DAT_00342b70[];

// Descriptive wrapper for FUN_002663a0
void set_global_event_flag(uint32_t bit_index)
{
  uint32_t byte_index = bit_index >> 3; // divide by 8
  if (byte_index < 0x900)
  { // bounds: 0x900 bytes
    uint8_t mask = (uint8_t)(1u << (bit_index & 7));
    DAT_00342b70[byte_index] |= mask;
  }
}
