/**
 * Text rendering function - renders a string of text character by character
 *
 * This function processes a null-terminated string and renders each printable
 * character to the screen. It filters out non-printable characters and advances
 * the X position for each character rendered.
 *
 * Original function: FUN_00268498
 * Address: 0x00268498
 *
 * @param text Null-terminated string to render
 * @param x_pos Starting X coordinate for text
 * @param y_pos Y coordinate for text (passed through to character renderer)
 */
void render_text_string(byte *text, int x_pos, void *y_pos)
{
  byte current_char;

  while (true)
  {
    current_char = *text;
    text = text + 1;
    if (current_char == 0) // End of string
      break;

    // Check if character is printable (ASCII 0x20-0x7F range)
    if (current_char - 0x20 < 0x60)
    {
      render_character(current_char, x_pos, y_pos);
      x_pos = x_pos + 0xc; // Advance X position by 12 pixels per character
    }
  }
  return;
}

// Function prototype for referenced function:

/**
 * Character rendering function (already analyzed)
 * Original: FUN_00268410
 */
extern void render_character(uint character, void *x_pos, int y_pos);
