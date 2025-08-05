/*
 * Debug Printf Function - Variadic Debug Output
 * Original: FUN_0026c088
 *
 * This function implements a variadic printf-style debug output function that only
 * executes when debug flag 0x200 is set in DAT_003555d8. The function follows
 * the typical PS2 variadic calling convention by setting up a stack-based
 * argument array for the format processing.
 *
 * The function checks if bit 0x200 (512) is set in the global debug flags word
 * DAT_003555d8. This bit appears to control general debug output functionality.
 * If enabled, it sets up the variadic arguments in a stack array and calls
 * the sprintf formatter followed by a text processing function.
 *
 * Debug Flag Analysis:
 * - DAT_003555d8 & 0x200: Controls debug printf output
 * - This is a 16-bit debug flags register used throughout the system
 * - Multiple functions read this register for different debug features
 *
 * Call Chain:
 * 1. Check debug flag 0x200
 * 2. Setup variadic arguments on stack
 * 3. FUN_0030e0f8: sprintf_formatted - formats the string
 * 4. FUN_0030c0c0: Text processing/output function
 *
 * Usage Pattern:
 * debug_printf_variadic(format_string, arg1, arg2, arg3, ...);
 *
 * Original function: FUN_0026c088
 */

#include "orphen_globals.h"

// Forward declarations for debug system functions
extern int FUN_0030e0f8(char *output_buffer, void *format_string, void *args); // sprintf_formatted
extern void FUN_0030c0c0(char *formatted_text);                                // debug_text_processor

// Debug system globals
extern uint16_t DAT_003555d8; // Debug flags register (bit 0x200 = debug printf enabled)

/**
 * Variadic debug printf function
 * Only outputs when debug flag 0x200 is enabled
 *
 * @param format_string Format string for printf-style formatting
 * @param ... Variable arguments for format string
 */
void debug_printf_variadic(void *format_string, ...)
{
  char format_buffer[4096]; // Stack buffer for formatted output
  void *arg_array[7];       // Variadic argument array

  // Only execute debug output if flag 0x200 is set
  if ((DAT_003555d8 & 0x200) != 0)
  {
    // Set up variadic arguments in stack array (PS2 calling convention)
    // Note: First argument (format_string) is already in param_1
    // Starting from param_2 for variadic args
    arg_array[0] = (void *)((uint64_t *)&format_string)[1]; // param_2
    arg_array[1] = (void *)((uint64_t *)&format_string)[2]; // param_3
    arg_array[2] = (void *)((uint64_t *)&format_string)[3]; // param_4
    arg_array[3] = (void *)((uint64_t *)&format_string)[4]; // param_5
    arg_array[4] = (void *)((uint64_t *)&format_string)[5]; // param_6
    arg_array[5] = (void *)((uint64_t *)&format_string)[6]; // param_7
    arg_array[6] = (void *)((uint64_t *)&format_string)[7]; // param_8

    // Format the string using sprintf-style function
    FUN_0030e0f8(format_buffer, format_string, arg_array);

    // Process/output the formatted text
    FUN_0030c0c0(format_buffer);
  }
}
