#include <stdint.h>

// PS2 type definitions (matching Ghidra output)
typedef unsigned char byte;
typedef unsigned int uint;

/**
 * Flag state checking function - checks if a specific game flag is set
 *
 * This function checks whether a particular game flag is set by reading from
 * a bit array stored in memory. Each flag is represented by a single bit,
 * allowing efficient storage of thousands of boolean game state values.
 *
 * Original function: FUN_00266368
 * Address: 0x00266368
 *
 * @param flag_index Index of the flag to check (0-based)
 * @return Non-zero if flag is set, 0 if flag is clear or index out of bounds
 */
uint get_flag_state(uint flag_index)
{
  // Check if flag index is within valid range
  // 0x8ff = 2303, so maximum flag index is 2303 * 8 = 18424
  if (0x8ff < (int)flag_index >> 3)
  {
    return 0; // Out of bounds, return false
  }

  // Calculate byte index (flag_index / 8) and bit position (flag_index % 8)
  // Read the appropriate byte from the flag array and check the specific bit
  return (uint)(game_flags_array[(int)flag_index >> 3]) & (1 << (flag_index & 7));
}

// Global flag array definition:

/**
 * Global game flags bit array
 * Original: DAT_00342b70
 *
 * This array stores all game flags as individual bits, allowing up to
 * approximately 18,424 different boolean game state values.
 */
extern byte game_flags_array[];
