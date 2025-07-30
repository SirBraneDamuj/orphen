#include <stdint.h>

// PS2 type definitions (matching Ghidra output)
typedef unsigned char undefined;
typedef unsigned char undefined1;
typedef unsigned short undefined2;
typedef unsigned int undefined4;

/**
 * Clear memory buffer function - zeros out a 256-byte buffer
 *
 * This function clears a buffer by zeroing 64 dwords (256 bytes) starting
 * from a calculated address. It only performs the clear operation if the
 * base address is non-zero.
 *
 * The function clears from (base_address + 0x100) downward for 64 dwords,
 * which suggests it might be clearing some kind of stack or buffer structure.
 *
 * Original function: FUN_0025bc30
 * Address: 0x0025bc30
 */
void clear_memory_buffer(void)
{
  undefined4 *buffer_ptr;
  int dword_count;

  // Calculate buffer start address (base + 256 bytes)
  buffer_ptr = (undefined4 *)(buffer_base_address + 0x100);

  // Only clear if base address is valid
  if (buffer_base_address != 0)
  {
    dword_count = 0x40; // 64 dwords = 256 bytes
    do
    {
      *buffer_ptr = 0; // Clear current dword
      dword_count = dword_count - 1;
      buffer_ptr = buffer_ptr - 1; // Move backward through buffer
    } while (-1 < dword_count);
  }
  return;
}

// Global variable reference:

/**
 * Buffer base address - pointer to buffer structure
 * Original: iGpffffbd84
 */
extern int buffer_base_address;
