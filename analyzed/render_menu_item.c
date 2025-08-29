#include "orphen_globals.h"

// Local fallback primitive aliases (in case the shared Ghidra typedef header is not included
// by the tooling environment invoking this single translation unit). These guards avoid
// redefinition if the global header already provided them.
#ifndef ORPHEN_PRIMITIVE_TYPEDEFS
#define ORPHEN_PRIMITIVE_TYPEDEFS
typedef unsigned char byte;
typedef unsigned char undefined1;
typedef unsigned int uint;
typedef unsigned int undefined4;
typedef unsigned long long undefined8;
#endif

// Forward declarations for referenced functions
extern uint *get_text_resource(int text_index);                                                       // FUN_0025b9e8
extern void FUN_002318c0(undefined4 param1, int param2, void *param3, unsigned int param4);           // Menu item state/setup
extern short calculate_text_width(char *text_string, int scale);                                      // FUN_00238e68
extern void render_text_with_scaling(int x, int y, char *text, uint color, int scale_x, int scale_y); // FUN_00238608
extern void FUN_00231c30(int x, int y, int width, int height);                                        // Background bar
extern void FUN_00239020(undefined4 *gpu_packet);                                                     // Packet submit
extern void graphics_buffer_overflow_handler(int error_code);                                         // FUN_0026bf90

// Local analyzing definitions (standalone). In the full project these should map to real symbols.
int menu_calculated_width = 0;                           // DAT_0031c504
int menu_current_y_position = 0;                         // DAT_0031c468
undefined4 menu_state_buffer = 0;                        // uGpffffbcb8
byte menu_color_array[16] = {0};                         // gp0xffffae74
byte menu_selection_flags[16] = {0};                     // gp0xffffb698
byte current_selection_flag = 0;                         // bGpffffae38
byte selection_flag_array[16] = {0};                     // additional flags
short selection_texture_u_coords[32] = {0};              // DAT_0031c220
short selection_texture_v_coords[32] = {0};              // DAT_0031c222
undefined4 scratchpad_raw_area[0x4000 / 4];              // simulate scratchpad space
undefined4 *scratchpad_buffer_ptr = scratchpad_raw_area; // DAT_70000000 equivalent

// Analysis: FUN_00231e60 (see repository docs for expanded commentary).

void render_menu_item(long slot, undefined8 text_id)
{
  short texture_coord;
  undefined4 *gpu_packet;
  int text_width_pixels; // Measured width of the localized string
  undefined8 text_resource;
  int selection_indicator_index;
  int slot_index;
  int y_position;
  int half_menu_width; // Cached -menu_calculated_width/2 for repeated offsets

  // Get menu layout parameters
  int menu_width = menu_calculated_width;
  slot_index = (int)slot;
  y_position = menu_current_y_position + (slot_index + -3) * -0x1e; // Calculate Y position (30-pixel spacing)

  // Get text resource and set up menu item
  text_resource = get_text_resource(text_id);
  FUN_002318c0(menu_state_buffer, slot_index + -4, &menu_color_array + slot_index, 0x2080);

  // Calculate text width and render text
  text_width_pixels = calculate_text_width((char *)text_resource, 0x14);
  render_text_with_scaling(-text_width_pixels / 2 - 0x20, y_position, (char *)text_resource,
                           (uint)(byte)menu_color_array[slot_index] << 0x18 | 0x808080,
                           0x14, 0x16); // Text with calculated color

  // Render background box
  half_menu_width = -menu_width / 2;
  FUN_00231c30(half_menu_width - 0x20, y_position + 2, menu_width, 0x1a);

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
    gpu_packet[2] = half_menu_width - 0x3a;                                                // X position (icon)
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

// (Redundant extern documentation block removed; declarations appear near top.)
