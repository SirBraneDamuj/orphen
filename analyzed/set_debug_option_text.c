/*
 * Debug Option Text Setter - FUN_00268c98
 *
 * Sets debug option display text to either "ON " or "OFF" based on a condition.
 * This function modifies a 3-character string buffer to show the current state
 * of debug display options.
 *
 * The function writes ASCII characters directly to the buffer:
 * - When enabled (condition != 0): Sets to "ON " (0x4F, 0x4E, 0x20)
 * - When disabled (condition == 0): Sets to "OFF" (0x4F, 0x46, 0x46)
 *
 * Original function: FUN_00268c98
 */

#include "orphen_globals.h"

/*
 * Sets debug option text to "ON " or "OFF" based on condition
 *
 * Parameters:
 *   text_buffer - Pointer to 3-byte character buffer to modify
 *   condition - If non-zero, sets "ON ", if zero sets "OFF"
 *
 * The buffer is modified in-place with ASCII characters:
 * - text_buffer[0] = 'O' (0x4F) - always 'O'
 * - text_buffer[1] = 'N' (0x4E) or 'F' (0x46)
 * - text_buffer[2] = ' ' (0x20) or 'F' (0x46)
 */
void set_debug_option_text(unsigned char *text_buffer, long condition)
{
  if (condition != 0)
  {
    // Set to "ON "
    text_buffer[0] = 0x4F; // 'O'
    text_buffer[1] = 0x4E; // 'N'
    text_buffer[2] = 0x20; // ' ' (space)
  }
  else
  {
    // Set to "OFF"
    text_buffer[0] = 0x4F; // 'O'
    text_buffer[1] = 0x46; // 'F'
    text_buffer[2] = 0x46; // 'F'
  }
}
