/*
 * Scene Work Display Debug Menu - FUN_0026a508
 *
 * Handles the "SCEN WORK DISP" debug submenu that allows toggling 128 individual
 * scene work flags (0-127). This appears to be a debug system for enabling/disabling
 * visibility or processing of different scene elements during development.
 *
 * The submenu displays:
 * - "SCEN WORK DISP" title
 * - Current selected index (0-127)
 * - Current state of selected flag ("ON" or "OFF")
 *
 * Controls:
 * - Up/Down D-pad: Navigate flag index (with wraparound)
 * - Left/Right D-pad: Change index by 10
 * - Triangle/X: Change index by 10
 * - Circle: Toggle selected flag on/off
 * - Start: Exit submenu
 *
 * The flags are stored as bits in a 4-integer array (128 bits total).
 *
 * Original function: FUN_0026a508
 */

#include "orphen_globals.h"

// Forward declarations for functions not yet analyzed
extern int FUN_002685e8(void *text_ptr);                            // calculate_text_width
extern void FUN_00268498(void *text_ptr, int x, int y);             // render_menu_text
extern void FUN_0030c1d8(char *buffer, int format_addr, int value); // sprintf_formatted
extern void FUN_00268500(char *src, int dst_addr);                  // copy_string
extern void FUN_00268650(int x, int y, int w, int h, int color);    // render_menu_rectangle
extern long FUN_0023b9f8(int input_mask, int param);                // check_controller_input
extern void FUN_002686a0(void);                                     // clear_controller_input_state

// Scene work display globals (not yet in orphen_globals.h)
extern unsigned int DAT_00355128;    // Current selected scene work index (0-127)
extern unsigned int DAT_0031e770[4]; // Scene work flags array (128 bits across 4 integers)

/*
 * Handles the SCEN WORK DISP debug submenu interface
 *
 * Displays a submenu allowing developers to toggle individual scene work flags.
 * Each flag (0-127) can be enabled/disabled, likely controlling visibility or
 * processing of different scene elements for debugging purposes.
 *
 * Returns:
 *   int - Menu state:
 *         0: Continue displaying submenu
 *         1: Exit submenu (Start button pressed)
 */
int scene_work_display_debug_menu(void)
{
  int text_width;
  int menu_x;
  unsigned int flag_array_index;
  int navigation_delta;
  long controller_input;
  char index_display_buffer[64];

  // Calculate text width and center the title
  text_width = FUN_002685e8((void *)0x34d610); // "SCEN WORK DISP" width
  text_width = text_width * 0xc;               // Convert to pixels
  menu_x = -(text_width >> 1);                 // Center horizontally

  // Render title
  FUN_00268498((void *)0x34d610, menu_x, 0); // "SCEN WORK DISP"

  // Format current index display
  FUN_0030c1d8(index_display_buffer, 0x355130, DAT_00355128);

  // Determine bit position for current index
  flag_array_index = DAT_00355128 + 0x1f;
  if (-1 < (int)DAT_00355128)
  {
    flag_array_index = DAT_00355128;
  }

  // Check if current flag is set and display appropriate state
  if (((int)(DAT_0031e770[(int)flag_array_index >> 5]) >> (DAT_00355128 & 0x1f) & 1U) == 0)
  {
    // Flag is OFF
    FUN_00268500(index_display_buffer, 0x355140); // Append "OFF" text
  }
  else
  {
    // Flag is ON
    FUN_00268500(index_display_buffer, 0x355138); // Append "ON" text
  }

  // Render index and state display
  FUN_00268498(index_display_buffer, menu_x + 0x10, -20);

  // Render menu borders
  FUN_00268650(menu_x + -4, 0, text_width + 4, 0x14, 0x6000);   // Top border
  FUN_00268650(menu_x + -4, 4, text_width + 8, 0x30, 0x600000); // Main border

  // Process controller input
  controller_input = FUN_0023b9f8(0x500c, 0);

  if (controller_input != 0)
  {
    // Handle navigation input
    if ((DAT_003555f4 & 0x1000) == 0)
    { // Up D-pad not pressed
      if ((DAT_003555f4 & 0x4000) == 0)
      { // Down D-pad not pressed
        if ((DAT_003555f4 & 4) == 0)
        { // Triangle not pressed
          navigation_delta = 10;
          if ((DAT_003555f4 & 8) == 0)
          { // X not pressed
            navigation_delta = 0;
          }
        }
        else
        {
          navigation_delta = -10; // Triangle: decrease by 10
        }
      }
      else
      {
        navigation_delta = -1; // Down D-pad: decrease by 1
      }
    }
    else
    {
      navigation_delta = 1; // Up D-pad: increase by 1
    }

    // Apply navigation delta with wraparound
    DAT_00355128 = DAT_00355128 + navigation_delta;
    if ((int)DAT_00355128 < 0)
    {
      DAT_00355128 = 0x7f; // Wrap to 127
    }
    else if (0x7f < (int)DAT_00355128)
    {
      DAT_00355128 = 0; // Wrap to 0
    }
  }

  // Handle action buttons
  if ((DAT_003555f6 & 0x100) == 0)
  { // Start button not pressed
    if ((DAT_003555f6 & 0x20) != 0)
    { // Circle button pressed - toggle flag
      // Calculate which array element and bit position
      flag_array_index = DAT_00355128 + 0x1f;
      if (-1 < (int)DAT_00355128)
      {
        flag_array_index = DAT_00355128;
      }

      // Toggle the bit using XOR
      DAT_0031e770[(int)flag_array_index >> 5] =
          DAT_0031e770[(int)flag_array_index >> 5] ^ (1 << (DAT_00355128 & 0x1f));
    }

    // Clear input state and continue showing submenu
    FUN_002686a0();
    return 1; // Continue submenu
  }

  // Start button pressed - exit submenu
  return 0;
}
