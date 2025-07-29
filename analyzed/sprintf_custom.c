#include <stdint.h>

// PS2 type definitions (matching Ghidra output)
typedef unsigned char undefined;
typedef unsigned char undefined1;
typedef unsigned short undefined2;
typedef unsigned int undefined4;

/**
 * String formatting function - formats text to a string buffer
 *
 * This function appears to set up a string buffer structure and format text into it.
 * This is likely sprintf() or a similar string formatting function.
 *
 * Original function: FUN_0030e0f8
 * Address: 0x0030e0f8
 *
 * @param output_buffer Buffer to write formatted text to
 */
void sprintf_custom(undefined1 *output_buffer)
{
  // Set up buffer structure for string formatting
  undefined1 *buffer_array[2];
  undefined4 buffer_size;
  undefined2 buffer_flags;
  undefined1 *buffer_ptr;
  undefined4 max_size;
  undefined *stream_ptr;

  buffer_flags = 0x208;                  // Buffer flags for string output
  max_size = 0x7fffffff;                 // Maximum buffer size
  stream_ptr = get_default_stream_ptr(); // Get default stream pointer
  buffer_size = 0x7fffffff;              // Buffer size
  buffer_array[0] = output_buffer;
  buffer_ptr = output_buffer;

  // Call fprintf function with buffer structure
  fprintf_custom(buffer_array);

  // Null-terminate the string
  *buffer_array[0] = 0;
  return;
}

// Function prototypes for referenced functions:

/**
 * Get default stream pointer
 * Original: PTR_DAT_0034b01c
 */
extern undefined *get_default_stream_ptr(void);

/**
 * Formatted print wrapper function (already analyzed)
 * Original: FUN_0030c8a0
 */
extern void fprintf_custom(void *stream_struct);
