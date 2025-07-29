#include <stdint.h>

// PS2 type definitions (matching Ghidra output)
typedef unsigned char undefined;
typedef unsigned char undefined1;
typedef unsigned short undefined2;
typedef unsigned int undefined4;
typedef unsigned long long undefined8;

// Forward declaration for the graphics primitive renderer
extern void render_graphics_primitive(unsigned long long render_flags, long texture_info, int x_pos, int y_pos,
                                      short width, short height, int texture_u, int texture_v,
                                      int texture_width, int texture_height, int color_data, undefined4 gpu_command);

/**
 * Simplified graphics primitive wrapper function
 *
 * This function provides a simplified interface to the graphics primitive renderer
 * with preset parameters for common rendering operations. It appears to be used
 * for rendering simple primitives with specific default settings.
 *
 * Original function: FUN_00268650
 * Address: 0x00268650
 *
 * @param position_data Position/coordinate data (x/y packed?)
 * @param y_offset Y coordinate offset (gets negated)
 * @param size_data Size/dimension data (width/height packed?)
 * @param color_data Color/texture data
 */
void render_simple_primitive(undefined8 position_data, int y_offset, undefined8 size_data, undefined8 color_data)
{
  // Call the main graphics primitive renderer with preset parameters:
  // - render_flags = 1 (basic rendering mode)
  // - texture_info = 0xffffffffffffeff7 (special texture mode, likely untextured)
  // - position_data passed through as-is
  // - y_offset is negated
  // - size_data passed through as-is
  // - color_data passed through as-is
  // - texture coordinates = 0xffffffffffffffff (invalid/unused)
  // - gpu_command = 0 (default command)
  render_graphics_primitive(1, 0xffffffffffffeff7, position_data, -y_offset, size_data, color_data,
                            0xffffffffffffffff, 0);
  return;
}
