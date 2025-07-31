/*
 * Set Game Flag State
 *
 * Sets a specific bit in the game flags array to 1 (true state).
 * The flag ID is converted to byte offset and bit position within that byte.
 *
 * Parameters:
 *   flag_id - The flag identifier to set (0-18423)
 *
 * Implementation:
 *   - Byte offset = flag_id >> 3 (divide by 8)
 *   - Bit position = flag_id & 7 (remainder when divided by 8)
 *   - Safety check ensures offset < 0x900 (2304 bytes max)
 *
 * Original function: FUN_002663a0
 * Original address: 0x002663a0
 */

#include "orphen_globals.h"

void set_flag_state(uint flag_id)
{
  int byte_offset = (int)flag_id >> 3; // Divide by 8 to get byte offset

  // Safety check: ensure we don't write beyond the flags array
  if (byte_offset < 0x900)
  {                                 // 0x900 = 2304 bytes = size of game_flags_array
    int bit_position = flag_id & 7; // Get bit position within byte (0-7)
    game_flags_array[byte_offset] |= (unsigned char)(1 << bit_position);
  }
}
