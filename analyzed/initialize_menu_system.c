#include "orphen_globals.h"

// Forward declarations for referenced functions
extern undefined4 map_menu_item_to_index(undefined4 menu_item_id); // Map menu ID to index (FUN_002298d0)
extern undefined8 FUN_0025b9e8(uint text_id);                      // Get text resource (likely)
extern long64 FUN_00238e68(undefined8 text_resource, uint width);  // Calculate text width
extern void activate_menu_with_audio(void);                        // Activate menu with audio (FUN_00237ad8)

/**
 * Initialize menu system with availability checks and layout
 *
 * This function sets up a menu system with 7 menu items. It performs several operations:
 * 1. Checks availability of menu items using function pointers in PTR_FUN_0031c3c0 array
 * 2. Sets availability flags in menu_availability_mask (bit field)
 * 3. Special handling for menu item 6 based on some game state
 * 4. Calculates text widths for menu items (text IDs 0x3f-0x45)
 * 5. Sets up display positions and colors for each menu item
 * 6. Colors: 0x20404040 for unavailable items, 0x20808080 for available items
 * 7. Positions items vertically with 0x1e (30) pixel spacing
 *
 * The function uses several data arrays:
 * - PTR_FUN_0031c3c0: Array of function pointers to check item availability
 * - DAT_0031c45c/DAT_0031c464: Menu display data arrays (positions, colors, etc.)
 *
 * Original function: FUN_00231a98
 * Address: 0x00231a98
 */
void initialize_menu_system(void)
{
  int availability_check_result;
  undefined8 text_resource;
  long64 text_width;
  uint menu_item_index;
  undefined **availability_check_functions;
  int *menu_color_ptr;
  int *menu_position_ptr;
  long64 max_text_width;
  int y_position;

  y_position = 0x70;                                // Starting Y position (112 pixels)
  max_text_width = 0;                               // Track maximum text width
  availability_check_functions = &PTR_FUN_0031c3c0; // Function pointer array
  menu_item_index = 0;
  menu_availability_mask = 0xffff; // Initially all items available
  menu_display_state = 0;          // Reset display state

  // First pass: Check availability of each menu item
  do
  {
    if (*availability_check_functions == (undefined *)0x0)
    {
      // No availability function - mark item as unavailable
      menu_availability_mask = menu_availability_mask & ~(ushort)(1 << (menu_item_index & 0x1f));
    }
    else if ((menu_item_index == 6) &&
             (availability_check_result = map_menu_item_to_index(game_mode_state),
              availability_check_result - 1U < 2))
    {
      // Special case for menu item 6: check game mode state
      // If game mode maps to index 1 or 2, disable item 6 (clear bit 6)
      menu_availability_mask = menu_availability_mask & 0xffbf; // Clear bit 6 (0x40)
    }
    menu_item_index = menu_item_index + 1;
    availability_check_functions = availability_check_functions + 1;
  } while ((int)menu_item_index < 7);

  // Second pass: Set up display properties for each menu item
  menu_position_ptr = &menu_position_array; // Y positions
  menu_item_index = 0;
  menu_color_ptr = &menu_color_array; // Colors

  do
  {
    // Get text resource for this menu item (text IDs 0x3f to 0x45)
    text_resource = FUN_0025b9e8(menu_item_index + 0x3f);

    // Calculate text width with specific parameters
    text_width = FUN_00238e68(text_resource, 0x14);

    // Track maximum width for centering
    if (max_text_width < text_width)
    {
      max_text_width = text_width;
    }

    // Set color based on availability
    if (((int)(uint)menu_availability_mask >> (menu_item_index & 0x1f) & 1U) == 0)
    {
      // Item unavailable - dark gray color
      menu_color_ptr[1] = 0x20404040;
    }
    else
    {
      // Item available - lighter gray color
      menu_color_ptr[1] = 0x20808080;
    }

    // Set Y position
    menu_position_ptr[1] = y_position;

    menu_item_index = menu_item_index + 1;
    y_position = y_position + -0x1e; // Move up 30 pixels for next item

    // Calculate X position for centering (negative offset from center)
    availability_check_result = *menu_color_ptr;
    menu_color_ptr = menu_color_ptr + 6;                          // Move to next menu item data
    *menu_position_ptr = (availability_check_result * -0x14) / 2; // Center calculation
    menu_position_ptr = menu_position_ptr + 6;                    // Move to next menu item data
  } while ((int)menu_item_index < 7);

  // Set menu width and finalize setup
  menu_width = (int)max_text_width + 0x20; // Add padding to maximum width
  menu_selection_index = 0;                // Reset selection to first item

  // Activate the menu with audio feedback
  activate_menu_with_audio();
  return;
}

// Global variables for menu system:

/**
 * Menu availability bitmask
 * Each bit represents whether a menu item is available (1) or unavailable (0)
 * Original: uGpffffbcc0
 */
extern ushort menu_availability_mask;

/**
 * Menu display state
 * Original: DAT_0031c458
 */
extern int menu_display_state;

/**
 * Game mode state used for menu availability checks
 * Original: DAT_0058beb0
 */
extern int game_mode_state;

/**
 * Array of function pointers for checking menu item availability
 * Original: PTR_FUN_0031c3c0
 */
extern undefined *PTR_FUN_0031c3c0;

/**
 * Menu color array - stores color values for each menu item
 * Original: DAT_0031c45c (offset +4 for colors)
 */
extern int menu_color_array;

/**
 * Menu position array - stores position data for each menu item
 * Original: DAT_0031c464 (offset +4 for Y positions)
 */
extern int menu_position_array;

/**
 * Menu width (calculated from text widths)
 * Original: DAT_0031c45c
 */
extern int menu_width;

/**
 * Current menu selection index
 * Original: uGpffffbcbc
 */
extern ushort menu_selection_index;
