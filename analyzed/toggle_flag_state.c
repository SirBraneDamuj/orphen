#include <stdint.h>

// PS2 type definitions (matching Ghidra output)
typedef unsigned char undefined;
typedef unsigned char undefined1;
typedef unsigned short undefined2;
typedef unsigned int undefined4;
typedef unsigned int uint;
typedef unsigned char byte;

/**
 * Game flag toggle function - toggles a specific flag and returns its new state
 *
 * This function toggles (XORs) a specific bit in the game flags array and returns
 * whether the flag is now set (1) or cleared (0). It's used by the debug menu
 * system to toggle flag states when the player presses the toggle button.
 *
 * The function uses bit manipulation to:
 * 1. Calculate which byte contains the flag (flag_id >> 3)
 * 2. Calculate which bit within that byte (flag_id & 7)
 * 3. XOR that specific bit to toggle it
 * 4. Return the new state of that bit
 *
 * Original function: FUN_00266418
 * Address: 0x00266418
 *
 * @param flag_id The flag ID to toggle (0-18423 range)
 * @return 1 if flag is now set, 0 if flag is now cleared
 */
uint toggle_flag_state(uint flag_id)
{
  uint new_flag_value;
  uint bit_mask;

  // Calculate bit mask for the specific bit within the byte
  bit_mask = 1 << (flag_id & 7); // bit_position = flag_id % 8

  // Validate flag ID is within bounds (max 2303 bytes * 8 = 18424 flags)
  if (0x8ff < (int)flag_id >> 3) // if byte_index > 2303
  {
    return 0; // Invalid flag ID - return false
  }

  // Toggle the specific bit using XOR operation
  new_flag_value = (byte)game_flags_array[(int)flag_id >> 3] ^ bit_mask;

  // Store the updated byte back to the flags array
  game_flags_array[(int)flag_id >> 3] = (byte)new_flag_value;

  // Return whether the specific bit is now set (1) or cleared (0)
  return new_flag_value & 0xff & bit_mask;
}

// Global variable reference:

/**
 * Game flags bit array - stores all game state flags
 * Capacity: ~18,424 flags (2,303 bytes * 8 bits per byte)
 * Original address: DAT_00342b70
 */
extern unsigned char game_flags_array[2303];
