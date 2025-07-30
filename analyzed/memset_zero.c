#include <stdint.h>

// PS2 type definitions (matching Ghidra output)
typedef unsigned char undefined;
typedef unsigned char undefined1;
typedef unsigned short undefined2;
typedef unsigned int undefined4;
typedef unsigned int uint;

/**
 * Memory zeroing function - optimized memset implementation for clearing memory
 *
 * This function efficiently zeros out a block of memory by handling alignment
 * and using 32-bit writes where possible for better performance on PS2.
 *
 * The algorithm:
 * 1. Handle unaligned bytes at the start (align to 4-byte boundary)
 * 2. Zero memory in 4-byte chunks for efficiency
 * 3. Handle any remaining bytes at the end
 *
 * Original function: FUN_00267e78
 * Address: 0x00267e78
 *
 * @param memory_ptr Pointer to memory to zero
 * @param byte_count Number of bytes to zero
 */
void memset_zero(undefined4 *memory_ptr, uint byte_count)
{
  uint alignment_offset;

  // Check if pointer is aligned to 4-byte boundary
  alignment_offset = (uint)memory_ptr & 3;
  if (alignment_offset != 0)
  {
    // Handle unaligned bytes at start - align to 4-byte boundary
    byte_count = byte_count - alignment_offset;
    while (alignment_offset = alignment_offset - 1, alignment_offset != 0xffffffff)
    {
      *(undefined1 *)memory_ptr = 0;                    // Zero one byte
      memory_ptr = (undefined4 *)((int)memory_ptr + 1); // Advance by 1 byte
    }
  }

  // Calculate number of 4-byte chunks and remaining bytes
  uint dword_count = byte_count >> 2; // Number of 32-bit words
  byte_count = byte_count & 3;        // Remaining bytes after word alignment

  // Zero memory in 4-byte chunks for efficiency
  while (dword_count = dword_count - 1, dword_count != 0xffffffff)
  {
    *memory_ptr = 0;             // Zero 4 bytes at once
    memory_ptr = memory_ptr + 1; // Advance by 4 bytes
  }

  // Handle any remaining bytes (0-3 bytes)
  if (byte_count != 0)
  {
    while (byte_count = byte_count - 1, byte_count != 0xffffffff)
    {
      *(undefined1 *)memory_ptr = 0;                    // Zero one byte
      memory_ptr = (undefined4 *)((int)memory_ptr + 1); // Advance by 1 byte
    }
  }
  return;
}
