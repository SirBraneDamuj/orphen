#include <stdint.h>

// PS2 type definitions (matching Ghidra output)
typedef unsigned char undefined;
typedef unsigned char undefined1;
typedef unsigned short undefined2;
typedef unsigned int undefined4;
typedef unsigned int uint;

/**
 * Character rendering function - renders a single character at specified position
 *
 * This function appears to render a single character to the screen at the given
 * coordinates. It's called by the text rendering function for each printable character.
 *
 * Original function: FUN_00268410
 * Address: 0x00268410
 *
 * @param character Character to render (uint, only lower 8 bits used)
 * @param x_pos X coordinate for character placement
 * @param y_pos Y coordinate for character placement (may be combined with x_pos in param_2)
 */
void render_character(uint character, int x_pos, int y_pos)
{
  // Call low-level graphics rendering function
  // Parameters suggest this is setting up character rendering with:
  // - Character offset calculation: ((character & 0xff) - 0x20 & 0x1f) << 3 | 1
  // - This suggests characters start at ASCII 0x20 (space) and are 8 pixels wide
  render_graphics_primitive(0, 0xffffffffffffeff7, x_pos, -y_pos, 10, 0x14, 0x30,
                            ((character & 0xff) - 0x20 & 0x1f) << 3 | 1, 0, 0, 0, 0);
  return;
}

// Function prototype for referenced function:

/**
 * Low-level graphics primitive rendering function
 * Original: FUN_00207938
 */
extern void render_graphics_primitive(unsigned long long render_flags, long texture_info, int x_pos, int y_pos,
                                      short width, short height, int texture_u, int texture_v,
                                      int texture_width, int texture_height, int color_data, unsigned int gpu_command);
