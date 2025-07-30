#include "orphen_globals.h"

/**
 * Get character width from font width table
 *
 * This function looks up the width of a character from a font width table.
 * Each character has a predefined width stored in a lookup table, allowing
 * for variable-width font rendering where different characters take up
 * different amounts of horizontal space.
 *
 * The function uses the character code as an index into the width table,
 * masking to ensure it stays within valid range (0-255).
 *
 * Original function: FUN_00238e50
 * Address: 0x00238e50
 *
 * @param char_code Character code to look up (masked to 0-255 range)
 * @return Width of the character in pixels/units
 */
undefined1 get_character_width(uint char_code)
{
  return *(undefined1 *)((int)&font_width_table + (char_code & 0xff));
}

// Global variables for font system:

/**
 * Font character width table
 * Array containing width values for each character (0-255)
 * Original: PTR_DAT_0031c518
 */
extern undefined1 font_width_table;
