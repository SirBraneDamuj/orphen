#include <stdint.h>

// PS2 type definitions (matching Ghidra output)
typedef unsigned char undefined;
typedef unsigned char undefined1;
typedef unsigned short undefined2;
typedef unsigned int undefined4;
typedef unsigned long long undefined8;

/**
 * String formatting function with variadic arguments - formats text to a string buffer
 *
 * This function formats a string using printf-style format specifiers and writes
 * the result to a buffer. This is the main sprintf() implementation that takes
 * a format string and variable arguments, similar to the C standard library sprintf().
 *
 * Original function: FUN_0030c1d8
 * Address: 0x0030c1d8
 *
 * @param output_buffer Buffer to write formatted text to
 * @param format_string Format string with printf-style specifiers (%d, %s, etc.)
 * @param arg1-arg6 Variable arguments for format specifiers
 */
void sprintf_variadic(undefined1 *output_buffer, undefined8 format_string, undefined8 arg1, undefined8 arg2,
                      undefined8 arg3, undefined8 arg4, undefined8 arg5, undefined8 arg6)
{
  // Set up buffer structure for string formatting
  undefined1 *buffer_array[2];
  undefined4 buffer_size;
  undefined2 buffer_flags;
  undefined1 *buffer_ptr;
  undefined4 max_size;
  undefined *stream_ptr;

  // Stack frame for variadic arguments
  undefined8 args_stack[6];

  buffer_flags = 0x208;                  // Buffer flags for string output
  max_size = 0x7fffffff;                 // Maximum buffer size
  stream_ptr = get_default_stream_ptr(); // Get default stream pointer
  buffer_size = 0x7fffffff;              // Buffer size
  buffer_array[0] = output_buffer;
  buffer_ptr = output_buffer;

  // Set up argument stack for printf-style formatting
  args_stack[0] = arg1;
  args_stack[1] = arg2;
  args_stack[2] = arg3;
  args_stack[3] = arg4;
  args_stack[4] = arg5;
  args_stack[5] = arg6;

  // Call fprintf function with buffer structure, format string, and arguments
  fprintf_custom(buffer_array, format_string, &args_stack[0]);

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
extern void fprintf_custom(undefined1 *buffer_array[], undefined8 format_string, undefined8 *args);
