/*
 * Debug Text Output Function - Variadic Text Processor
 * Original: FUN_0030c0c0
 *
 * This function processes formatted text output through a stream-like structure
 * for debug output. It appears to be part of a printf/fprintf-style text output
 * system that routes debug text to an output stream or buffer.
 *
 * The function works with a global stream structure (PTR_DAT_0034b01c) that
 * points to a text output handler. This structure contains function pointers
 * and state information for managing text output operations.
 *
 * Structure Analysis:
 * - PTR_DAT_0034b01c: Global pointer to output stream structure (at 0x34ad30)
 * - stream+8: Offset to function pointer/handler within the stream structure
 * - stream+0x54: Offset to stream state/pointer field
 *
 * The function sets up the stream state and calls the underlying text processor
 * with variadic arguments, following the typical PS2 calling convention for
 * printf-style functions.
 *
 * Usage Context:
 * Called by debug_printf_variadic after sprintf formatting to output the
 * final formatted text to the debug output system.
 *
 * Original function: FUN_0030c0c0
 */

#include "orphen_globals.h"

// Forward declarations for stream processing functions
extern void FUN_0030c8a0(void *stream_handler, void *text_data, void *args); // stream_output_processor

// Debug output stream structure (not yet fully analyzed)
extern void *PTR_DAT_0034b01c; // Global debug output stream pointer -> 0x34ad30

/**
 * Process and output formatted debug text
 *
 * This function takes formatted text from debug_printf_variadic and processes
 * it through the debug output stream system. It handles variadic arguments
 * and routes them to the appropriate stream processor.
 *
 * @param text_data Formatted text string to output
 * @param ... Additional variadic arguments for text processing
 */
void debug_text_output_processor(void *text_data, ...)
{
  void *stream_structure; // Local copy of stream structure pointer
  void *arg_array[7];     // Variadic argument array

  // Get the global debug output stream structure
  stream_structure = PTR_DAT_0034b01c;

  // Set up stream state - store stream pointer in its own state field
  // This appears to be setting up a circular reference for stream management
  *(void **)(*(int *)(stream_structure + 8) + 0x54) = stream_structure;

  // Set up variadic arguments in stack array (PS2 calling convention)
  // Starting from param_2 since param_1 (text_data) is already available
  arg_array[0] = (void *)((uint64_t *)&text_data)[1]; // param_2
  arg_array[1] = (void *)((uint64_t *)&text_data)[2]; // param_3
  arg_array[2] = (void *)((uint64_t *)&text_data)[3]; // param_4
  arg_array[3] = (void *)((uint64_t *)&text_data)[4]; // param_5
  arg_array[4] = (void *)((uint64_t *)&text_data)[5]; // param_6
  arg_array[5] = (void *)((uint64_t *)&text_data)[6]; // param_7
  arg_array[6] = (void *)((uint64_t *)&text_data)[7]; // param_8

  // Process the text through the stream output system
  // Uses the function pointer at offset 8 in the stream structure
  FUN_0030c8a0(*(void **)(stream_structure + 8), text_data, arg_array);
}
