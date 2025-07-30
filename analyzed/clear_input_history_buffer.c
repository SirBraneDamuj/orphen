#include <stdint.h>

// PS2 type definitions (matching Ghidra output)
typedef unsigned char undefined;
typedef unsigned char undefined1;
typedef unsigned short undefined2;
typedef unsigned int undefined4;
typedef unsigned int uint;

// Forward declaration for memory zeroing function
extern void memset_zero(undefined4 *memory_ptr, uint byte_count);

/**
 * Clear input history buffer
 *
 * This function clears the controller input history ring buffer by zeroing
 * out the entire buffer array. The buffer stores 64 entries of 4 bytes each
 * (256 bytes total = 0x100 bytes).
 *
 * This is typically called when resetting the input system or clearing
 * recorded input history.
 *
 * Original function: FUN_0023bae8
 * Address: 0x0023bae8
 */
void clear_input_history_buffer(void)
{
  // Clear the input history buffer (64 entries * 4 bytes = 256 bytes)
  // Address 0x342a70 is the input history ring buffer
  memset_zero((undefined4 *)0x342a70, 0x100);
  return;
}

// Global variable reference:

/**
 * Input history ring buffer (64 entries of 4 bytes each)
 * Stores controller input history for replay or analysis
 * Original address: 0x342a70 (referenced as DAT_00342a70)
 */
extern uint input_history_buffer[64]; // Located at 0x342a70
