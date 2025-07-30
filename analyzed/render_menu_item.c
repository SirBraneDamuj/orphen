#include "orphen_globals.h"

// Forward declarations for referenced functions
extern uint *get_text_resource(int text_index);                                                       // Get text resource by index (FUN_0025b9e8)
extern void FUN_002318c0(undefined4 param1, int param2, void *param3, unsigned int param4);           // Unknown function - possibly menu item setup
extern short calculate_text_width(char *text_string, int scale);                                      // Calculate text width (FUN_00238e68)
extern void render_text_with_scaling(int x, int y, char *text, uint color, int scale_x, int scale_y); // Render text with scaling (FUN_00238608)
extern void FUN_00231c30(int x, int y, int width, int height);                                        // Likely: render background/box
extern void FUN_00239020(undefined4 *gpu_packet);                                                     // Submit GPU rendering packet
extern void graphics_buffer_overflow_handler(int error_code);                                         // Buffer overflow handler (FUN_0026bf90)

/**
 * Render menu item with text and visual elements
 *
 * This function renders a complete menu item including:
 * 1. Menu item setup and state management
 * 2. Text retrieval and width calculation
 * 3. Text rendering with proper positioning and color
 * 4. Background/box rendering
 * 5. Selection indicator rendering (if applicable)
 *
 * The function uses a slot-based system where:
 * - Slots 4-8 correspond to menu items 0-4
 * - Each slot has associated position and color data
 * - Selection indicators are drawn based on current selection state
 *
 * Visual layout:
 * - Text is centered horizontally with offset calculations
 * - Background box is drawn slightly larger than text
 * - Selection indicators use different colors/textures based on state
 *
 * Original function: FUN_00231e60
 * Address: 0x00231e60
 *
 * @param slot Menu slot number (4-8 for menu items 0-4)
 * @param text_id Text resource ID to display (e.g., 0x50-0x54)
 */
void render_menu_item(long slot, undefined8 text_id)
{
  short texture_coord;
  undefined4 *gpu_packet;
  int text_width;
  undefined8 text_resource;
  int selection_indicator_index;
  int slot_index;
  int y_position;

  // Get menu layout parameters
  int menu_width = menu_calculated_width;
  slot_index = (int)slot;
  y_position = menu_current_y_position + (slot_index + -3) * -0x1e; // Calculate Y position (30-pixel spacing)

  // Get text resource and set up menu item
  text_resource = get_text_resource(text_id);
  FUN_002318c0(menu_state_buffer, slot_index + -4, &menu_color_array + slot_index, 0x2080);

  // Calculate text width and render text
  text_width = calculate_text_width((char *)text_resource, 0x14);
  render_text_with_scaling(-text_width / 2 + -0x20, y_position, (char *)text_resource,
                           (uint)(byte)menu_color_array[slot_index] << 0x18 | 0x808080,
                           0x14, 0x16); // Text with calculated color

  // Render background box
  text_width = -menu_width / 2;
  FUN_00231c30(text_width + -0x20, y_position + 2, menu_width, 0x1a);

  // Render selection indicator if slot is valid
  gpu_packet = scratchpad_buffer_ptr;
  if (slot < 8)
  {
    // Find appropriate selection indicator index
    selection_indicator_index = 0;
    if ((menu_selection_flags[slot_index] & current_selection_flag) == 0)
    {
      for (selection_indicator_index = 1;
           (selection_indicator_index < 8 &&
            ((menu_selection_flags[slot_index] & selection_flag_array[selection_indicator_index]) == 0));
           selection_indicator_index = selection_indicator_index + 1)
      {
      }
    }

    // Allocate scratchpad buffer for GPU packet
    scratchpad_buffer_ptr = scratchpad_buffer_ptr + 0x10;
    if ((undefined4 *)0x70003fff < scratchpad_buffer_ptr)
    {
      graphics_buffer_overflow_handler(0);
    }

    // Set up GPU packet for selection indicator
    gpu_packet[1] = 0xffffeff7;                                                            // Color/mode
    *gpu_packet = 0x82c;                                                                   // Packet type
    gpu_packet[2] = text_width + -0x3a;                                                    // X position
    gpu_packet[3] = y_position;                                                            // Y position
    gpu_packet[4] = 0x16;                                                                  // Width
    gpu_packet[5] = 0x16;                                                                  // Height
    gpu_packet[6] = (int)(short)selection_texture_u_coords[selection_indicator_index * 2]; // Texture U
    texture_coord = selection_texture_v_coords[selection_indicator_index * 2];
    *(undefined1 *)(gpu_packet + 10) = 0;
    gpu_packet[0xb] = 0;
    gpu_packet[7] = (int)texture_coord; // Texture V
    gpu_packet[8] = 0x20;               // Texture width
    gpu_packet[0xc] = 0x80808080;       // Color
    gpu_packet[9] = 0x20;               // Texture height

    // Submit GPU packet for rendering
    FUN_00239020(gpu_packet);

    // Release scratchpad buffer
    scratchpad_buffer_ptr = scratchpad_buffer_ptr + -0x10;
  }
  return;
}

// Global variables for menu item rendering:

/**
 * Menu state buffer for menu item setup
 * Original: uGpffffbcb8
 */
extern undefined4 menu_state_buffer;

/**
 * Menu color array - colors for each menu slot
 * Original: gp0xffffae74
 */
extern byte menu_color_array[];

/**
 * Menu selection flags - tracks selection state for each slot
 * Original: gp0xffffb698
 */
extern byte menu_selection_flags[];

/**
 * Current selection flag - used to check against selection flags
 * Original: bGpffffae38
 */
extern byte current_selection_flag;

/**
 * Selection flag array - array of flags for different selection states
 * Original: bGpffffae38 array
 */
extern byte selection_flag_array[];

/**
 * Selection indicator texture U coordinates
 * Array of texture coordinates for selection indicators
 * Original: DAT_0031c220
 */
extern short selection_texture_u_coords[];

/**
 * Selection indicator texture V coordinates
 * Array of texture coordinates for selection indicators
 * Original: DAT_0031c222
 */
extern short selection_texture_v_coords[];
