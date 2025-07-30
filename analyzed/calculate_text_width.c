#include "orphen_globals.h"

// Forward declarations
extern undefined1 get_character_width(uint char_code); // Get character width from font table (FUN_00238e50)

/**
 * Calculate total width of text string with scaling
 *
 * This function calculates the total pixel width of a text string by:
 * 1. Iterating through each character in the string
 * 2. Looking up each character's width from the font width table
 * 3. Applying scaling based on the provided scale parameter
 * 4. Summing all character widths to get total string width
 *
 * The scaling calculation is: (font_width * ((scale * 100) / 22)) / 100
 * This allows for proportional text scaling where scale=22 represents 100% size.
 *
 * Non-ASCII characters (negative char values) get a default width of 0x20 (32 pixels).
 * ASCII characters use the font width table with scaling applied.
 *
 * Original function: FUN_00238e68
 * Address: 0x00238e68
 *
 * @param text_string Null-terminated text string to measure
 * @param scale_factor Scaling factor (22 = 100%, higher = larger text)
 * @return Total width of the text string in pixels
 */
short calculate_text_width(char *text_string, int scale_factor)
{
  char current_char;
  short char_width;
  char *char_ptr;
  short total_width;
  int font_width;

  total_width = 0;
  current_char = *text_string;
  char_ptr = text_string + 1;

  while (current_char != '\0')
  {
    char_width = 0x20; // Default width for non-ASCII characters (32 pixels)

    if (-1 < current_char) // ASCII character (0-127)
    {
      // Look up character width from font table
      font_width = get_character_width((uint)current_char);

      // Apply scaling: (font_width * ((scale * 100) / 22)) / 100
      // Scale factor 22 represents 100% size
      char_width = (short)((font_width * ((scale_factor * 100) / 0x16)) / 100);
    }

    total_width = total_width + char_width;
    current_char = *char_ptr;
    char_ptr = char_ptr + 1;
  }

  return total_width;
}
