/**
 * Formatted print wrapper function - wrapper around vfprintf_custom
 *
 * This appears to be a wrapper function that calls the main vfprintf_custom function
 * with the stream extracted from a structure. This might be fprintf() or a similar
 * higher-level printing function.
 *
 * Original function: FUN_0030c8a0
 * Address: 0x0030c8a0
 *
 * @param stream_struct Structure containing stream information at offset 0x54
 * @param format_info Format information structure
 * @param format_string Format string containing text and format specifiers
 * @param args_list Pointer to variadic arguments list
 */
void fprintf_custom(void *stream_struct, void *format_info, void *format_string, void *args_list)
{
  // Extract stream from structure and call main formatting function
  vfprintf_custom(*(void **)((int)stream_struct + 0x54), stream_struct, format_string, args_list);
  return;
}

// Function prototype for referenced function:

/**
 * Main formatted output function (already analyzed)
 * Original: FUN_0030c8d0
 */
extern void vfprintf_custom(void *stream_info, void *param2, void *format_string, void *args_list);
