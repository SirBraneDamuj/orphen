/*
 * Debug Output Formatter - FUN_002681c0
 *
 * Formats and outputs debug messages to a debug buffer when debug output is enabled.
 * This function acts like a printf-style formatter that writes formatted text to
 * a global debug buffer rather than to stdout or a file.
 *
 * The function checks debug output flags and only processes output when enabled.
 * It formats the message using a sprintf-like function and appends it to a
 * global debug buffer at DAT_00572c38.
 *
 * Buffer management:
 * - Uses DAT_003551dc as current write position in buffer
 * - Has a maximum buffer size of 0x800 bytes (2048 bytes)
 * - Appends null terminator after each message
 *
 * Original function: FUN_002681c0
 */

#include "orphen_globals.h"

// Forward declarations for functions not yet analyzed
extern int FUN_0030e0f8(char *output_buffer, void *format_string, void *args); // sprintf_formatted
extern void FUN_00268558(char *dest, char *src);                               // strcpy_simple

// Debug output globals (not yet in orphen_globals.h)
extern char DAT_003555dc;        // Debug output enabled flag
extern char DAT_003555da;        // Debug system active flag
extern int DAT_003551dc;         // Current write position in debug buffer
extern char DAT_00572c38[0x800]; // Debug output buffer (2048 bytes)

/*
 * Formats and outputs debug messages to internal debug buffer
 *
 * Parameters:
 *   format_string - Pointer to format string (like printf format)
 *   param_2 through param_8 - Arguments for format string
 *
 * Only outputs when both debug flags are enabled:
 * - DAT_003555dc must be non-zero (debug output enabled)
 * - DAT_003555da must be non-zero (debug system active)
 *
 * Output is formatted using sprintf-like function and appended to
 * global debug buffer. Buffer has 2048 byte limit with overflow protection.
 */
void debug_output_formatter(void *format_string,
                            long param_2, long param_3, long param_4,
                            long param_5, long param_6, long param_7, long param_8)
{
  int formatted_length;
  char format_buffer[4096];
  long format_args[7];

  // Only proceed if debug output is enabled
  if (DAT_003555dc != '\0')
  {
    if (DAT_003555da == '\0')
    {
      // Debug system not active - disable debug output
      DAT_003555dc = '\0';
    }
    else
    {
      // Set up arguments array for sprintf-like formatting
      format_args[0] = param_2;
      format_args[1] = param_3;
      format_args[2] = param_4;
      format_args[3] = param_5;
      format_args[4] = param_6;
      format_args[5] = param_7;
      format_args[6] = param_8;

      // Format the message using sprintf-like function
      formatted_length = FUN_0030e0f8(format_buffer, format_string, format_args);

      // Check if there's room in the debug buffer
      if (DAT_003551dc + formatted_length < 0x800)
      {
        // Null-terminate the formatted string
        format_buffer[formatted_length] = 0;

        // Copy formatted message to debug buffer at current position
        FUN_00268558(&DAT_00572c38[DAT_003551dc], format_buffer);

        // Update write position in debug buffer
        DAT_003551dc = DAT_003551dc + formatted_length;
      }
      // If buffer would overflow, silently ignore the message
    }
  }
}
