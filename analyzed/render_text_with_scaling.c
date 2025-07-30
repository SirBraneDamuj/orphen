#include "orphen_globals.h"

// Forward declarations for referenced functions
extern void FUN_0026bf90(int param);                                 // Likely: GPU buffer overflow handler
extern int get_character_width(ulong char_code);                     // Get character width from font (FUN_00238e50)
extern void FUN_00239020(undefined4 *gpu_packet);                    // Likely: submit GPU rendering packet
extern void FUN_00231da0(int special_char_id, short *output_coords); // Likely: get special character coordinates

/**
 * Advanced text rendering function with scaling, color, and multi-font support
 *
 * This function renders a null-terminated text string character by character using
 * the PS2's GPU command buffer system. It handles:
 *
 * - GPU DMA packet construction and buffer management
 * - Variable-width font rendering with character width lookup
 * - Text scaling based on scale_x and scale_y (percentage-based, /22 scaling)
 * - Multiple character types:
 *   - Normal ASCII chars (0x20-0x98): Standard font with width lookup
 *   - Extended chars (0x99-0xFB): Different font texture (texture 0x2F vs 0x2E)
 *   - Special chars (0xFC+): Icon/symbol rendering with coordinate lookup
 * - Color support with 32-bit RGBA values
 * - Automatic X-position advancement after each character
 *
 * Font system details:
 * - Characters are arranged in an 11-column grid (% 0xB for X offset)
 * - Each character cell is 22x22 pixels (0x16 x 0x16)
 * - Font textures: 0x2E for normal chars, 0x2F for extended chars, 0x82C for special
 * - Scaling formula: (param * 100) / 22 for percentage-based scaling
 *
 * GPU packet format appears to use offset-based structure:
 * [0] = texture_id, [2] = x_pos, [3] = y_pos, [4] = width, [5] = height,
 * [6] = tex_u, [7] = tex_v, [8] = char_width, [9] = char_height, [12] = color
 *
 * Original function: FUN_00238608
 * Address: 0x00238608
 *
 * @param x_position X coordinate for text rendering
 * @param y_position Y coordinate for text rendering
 * @param text_string Null-terminated text string to render
 * @param color 32-bit RGBA color value
 * @param scale_x Horizontal scale factor (percentage, will be divided by 22)
 * @param scale_y Vertical scale factor (percentage, will be divided by 22)
 */
void render_text_with_scaling(int x_position, int y_position, char *text_string, uint color,
                              int scale_x, int scale_y)
{
  int char_width;
  undefined4 *gpu_packet;
  int scaled_width;
  int scaled_height;
  ulong current_char;
  short special_char_u;
  short special_char_v;
  int scaled_x_factor;

  // Get current GPU command buffer pointer and reserve space
  gpu_packet = gpu_command_buffer_ptr;
  gpu_command_buffer_ptr = gpu_command_buffer_ptr + 0x10; // Reserve 16 words

  // Check for buffer overflow
  if ((undefined4 *)0x70003fff < gpu_command_buffer_ptr)
  {
    FUN_0026bf90(0); // Handle GPU buffer overflow
  }

  // Initialize GPU packet with default texture and settings
  gpu_packet[1] = 0xffffeff7; // GPU command flags
  gpu_packet[0xb] = 1;        // Unknown parameter

  // Calculate scaling factors (percentage-based, divided by 22 for normalization)
  scaled_x_factor = (scale_x * 100) / 0x16;        // X scale factor
  scaled_height = ((scale_y * 100) / 0x16) * 0x16; // Y scale factor * 22

  // Set initial packet parameters
  gpu_packet[2] = x_position;           // X position
  gpu_packet[3] = y_position;           // Y position
  gpu_packet[0] = 0x2e;                 // Default font texture ID
  gpu_packet[0xc] = color;              // Text color
  gpu_packet[8] = 0x16;                 // Default character width (22)
  gpu_packet[9] = 0x16;                 // Default character height (22)
  *(undefined1 *)(gpu_packet + 10) = 0; // Clear parameter

  // Process each character in the string
  current_char = (ulong)*text_string;
  if (current_char != 0)
  {
    do
    {
      text_string = text_string + 1;

      if ((current_char & 0xff) < 0xfc)
      {
        // Handle normal and extended characters (0x00-0xFB)

        // Get character width from font table
        char_width = get_character_width(current_char);
        gpu_packet[0] = 0x2e; // Default font texture

        // Check if extended character range (0x99+)
        if (0x98 < (current_char & 0xff))
        {
          gpu_packet[0] = 0x2f; // Extended font texture
          current_char = (ulong)((int)(((uint)current_char - 0x79) * 0x1000000) >> 0x18);
        }

        // Set character width and calculate scaled dimensions
        gpu_packet[8] = char_width;                           // Character width
        int char_offset = ((uint)current_char & 0xff) - 0x20; // Offset from space character
        gpu_packet[4] = (char_width * scaled_x_factor) / 100; // Scaled width
        gpu_packet[5] = scaled_height / 100;                  // Scaled height

        // Calculate texture coordinates (11-column grid, 22x22 per cell)
        gpu_packet[6] = (char_offset % 0xb) * 0x16; // Texture U coordinate
        gpu_packet[7] = (char_offset / 0xb) * 0x16; // Texture V coordinate

        // Submit character rendering packet
        FUN_00239020(gpu_packet);

        // Advance X position by scaled character width
        gpu_packet[2] = gpu_packet[2] + gpu_packet[4];
      }
      else
      {
        // Handle special characters/icons (0xFC+)

        // Get special character texture coordinates
        FUN_00231da0(((uint)current_char & 0xff) - 0xf8, &special_char_u);

        scaled_width = scaled_height / 100;
        gpu_packet[0] = 0x82c;               // Special character texture
        gpu_packet[4] = scaled_width;        // Width
        gpu_packet[5] = scaled_width;        // Height (square)
        gpu_packet[9] = 0x20;                // Character height (32)
        gpu_packet[8] = 0x20;                // Character width (32)
        gpu_packet[6] = (int)special_char_u; // Texture U
        gpu_packet[7] = (int)special_char_v; // Texture V
        gpu_packet[0xc] = 0x80808080;        // Special character color

        // Submit special character packet
        FUN_00239020(gpu_packet);

        // Restore original settings
        gpu_packet[0xc] = color;                       // Restore original color
        gpu_packet[0] = 0x2e;                          // Restore normal font texture
        gpu_packet[2] = gpu_packet[2] + gpu_packet[4]; // Advance X position
        gpu_packet[8] = 0x16;                          // Restore normal width
        gpu_packet[9] = 0x16;                          // Restore normal height
      }

      current_char = (ulong)*text_string; // Get next character
    } while (current_char != 0);
  }

  // Release GPU command buffer space
  gpu_command_buffer_ptr = gpu_command_buffer_ptr + -0x10;
  return;
}

// Global variables for text rendering:

/**
 * GPU command buffer pointer for DMA packet construction
 * Points to current write position in GPU command buffer
 * Original: DAT_70000000
 */
extern undefined4 *gpu_command_buffer_ptr;
