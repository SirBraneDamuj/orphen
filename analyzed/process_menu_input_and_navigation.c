/*
 * Process Menu Input and Navigation - FUN_002686c8
 *
 * Handles menu display, navigation, and input processing for debug menu system.
 * This function renders menu items, processes controller input for navigation,
 * and returns the selected menu item index.
 *
 * The function performs several key operations:
 * 1. Calculates menu dimensions and layout
 * 2. Renders menu items with proper positioning and colors
 * 3. Handles scrolling for menus with more than 21 items
 * 4. Processes D-pad input for navigation
 * 5. Processes button input for selection/cancellation
 *
 * Menu navigation uses controller input:
 * - Up/Down D-pad: Navigate menu items
 * - Circle button: Select item (if valid)
 * - Start button: Cancel menu
 * - Triangle/X buttons: Alternative cancel options
 *
 * Original function: FUN_002686c8
 */

#include "orphen_globals.h"

// Forward declarations for functions not yet analyzed
extern int FUN_002685e8(void *text_ptr);                         // calculate_text_width
extern void FUN_00268498(void *text_ptr, int x, int y);          // render_menu_text
extern void FUN_00268650(int x, int y, int w, int h, int color); // render_menu_rectangle
extern long FUN_0023b9f8(int input_mask, int param);             // check_controller_input

// Menu system globals not yet in orphen_globals.h
extern int DAT_00355078;       // Current menu selection index
extern int DAT_0035507c;       // Menu scroll offset
extern void *PTR_DAT_00355070; // Current text color pointer
extern void *DAT_00ffffff;     // White color constant
extern void *DAT_00808080;     // Gray color constant

/*
 * Processes menu input and handles navigation
 *
 * Parameters:
 *   menu_items - Pointer to array of menu item text pointers
 *   item_states - Pointer to array of item state values (0=available, <0=disabled)
 *
 * Returns:
 *   int - Menu result:
 *         > 0: Selected menu item index (1-based)
 *         0: No selection made
 *         -900: Menu cancelled
 *         -901: Up navigation (special return code)
 *         -902: Down navigation (special return code)
 */
int process_menu_input_and_navigation(void **menu_items, int *item_states)
{
  char current_char;
  void *current_item;
  int item_count;
  long controller_input;
  int menu_width;
  int text_width;
  void **item_ptr;
  int *state_ptr;
  int display_height;
  int menu_x;
  int menu_y;
  int visible_items;
  int item_index;
  int max_items;
  int scroll_offset;

  // Count menu items and find maximum text width
  menu_width = 0;
  current_char = *(char *)*menu_items;
  item_count = 0;
  item_ptr = menu_items;

  while (current_char != '\0')
  {
    current_item = *item_ptr;
    item_count = item_count + 1;
    item_ptr = item_ptr + 1;
    text_width = FUN_002685e8(current_item); // Calculate text width
    current_char = *(char *)*item_ptr;
    if (menu_width < text_width)
    {
      menu_width = text_width;
    }
  }

  max_items = item_count - 1;

  // Ensure current selection is valid
  if (max_items < DAT_00355078)
  {
    DAT_00355078 = 1;
  }

  // Handle menu scrolling for large menus (more than 21 items)
  if (item_count < 0x16)
  { // 22 items or fewer
    DAT_0035507c = 0;
    visible_items = item_count;
    scroll_offset = max_items;
  }
  else
  {
    // Calculate scroll offset for large menus
    DAT_0035507c = DAT_00355078 + -0x14; // Current selection - 20
    if (DAT_0035507c < 0)
    {
      DAT_0035507c = 0;
    }
    scroll_offset = 0x14; // Show 20 items
    visible_items = 0x15; // 21 items total
  }

  // Calculate menu positioning
  menu_width = menu_width * 0xc;              // Width in pixels (12 pixels per character)
  display_height = visible_items * 0x14 >> 1; // Height calculation
  menu_x = -(menu_width >> 1);                // Center horizontally

  // Render main menu background
  FUN_00268498(*menu_items, menu_x, display_height);

  // Render scroll up indicator if needed
  if (DAT_0035507c != 0)
  {
    FUN_00268498((void *)0x355080, menu_x + menu_width + 8, display_height + -0x14);
  }

  // Render visible menu items
  int render_x = menu_x + -4;
  int current_selection = DAT_0035507c + 1;
  menu_items = menu_items + current_selection;
  int render_y = 1;
  menu_y = display_height;

  if ((*(char *)*menu_items != '\0') && (0 < scroll_offset))
  {
    state_ptr = (int *)(current_selection * 4 + (long)item_states);
    do
    {
      // Set text color based on item state
      PTR_DAT_00355070 = &DAT_00ffffff; // Default white
      if ((item_states != 0) && (*state_ptr < 0))
      {
        PTR_DAT_00355070 = &DAT_00808080; // Gray for disabled items
      }

      menu_y = menu_y + -0x14; // Move down 20 pixels
      FUN_00268498(*menu_items, menu_x, menu_y);
      menu_items = menu_items + 1;
      render_y = render_y + 1;
      state_ptr = state_ptr + 1;
      current_selection = current_selection + 1;
    } while ((*(char *)*menu_items != '\0') && (render_y <= scroll_offset));
  }

  // Reset text color
  PTR_DAT_00555070 = &DAT_00ffffff;

  // Render scroll down indicator if needed
  if (current_selection < item_count)
  {
    FUN_00268498((void *)0x355088, menu_x + menu_width + 8, menu_y);
  }

  // Render menu border rectangles
  FUN_00268650(render_x, display_height, menu_width + 4, 0x14, 0x6000); // Top border
  FUN_00268650(render_x, display_height + (DAT_00355078 - DAT_0035507c) * -0x14,
               menu_width + 4, 0x14, 0x80); // Selection highlight
  FUN_00268650(render_x, display_height + 4, menu_width + 8,
               display_height * 2 + 8, 0x600000); // Main border

  // Process controller input
  controller_input = FUN_0023b9f8(0xf000, 0); // Check for input
  item_count = DAT_00355078;

  if (controller_input != 0)
  {
    // Handle D-pad navigation
    if ((DAT_003555f4 & 0x1000) == 0)
    { // Up D-pad not pressed
      if ((DAT_003555f4 & 0x4000) == 0)
      { // Down D-pad not pressed
        if ((DAT_003555f4 & 0x8000) != 0)
        {
          return -0x385; // Left D-pad pressed (-901)
        }
        if ((DAT_003555f4 & 0x2000) != 0)
        {
          return -0x386; // Right D-pad pressed (-902)
        }
      }
      else
      {
        // Down D-pad pressed - move selection down
        item_count = DAT_00355078 + 1;
        if (max_items <= DAT_00355078)
        {
          DAT_00355078 = 1;
          item_count = DAT_00355078;
        }
      }
    }
    else
    {
      // Up D-pad pressed - move selection up
      item_count = max_items;
      if (1 < DAT_00355078)
      {
        item_count = DAT_00355078 + -1;
      }
    }
  }

  DAT_00355078 = item_count;
  item_count = DAT_00355078;

  // Handle action buttons
  if ((DAT_003555f6 & 0x20) == 0)
  { // Circle button not pressed
    if ((DAT_003555f6 & 0x100) == 0)
    {                    // Start button not pressed
      item_count = -900; // No selection, return cancel code
    }
    else
    {
      // Start button pressed - cancel menu
      item_count = 0;
      DAT_00355078 = 1;
    }
  }
  else if ((item_states == 0) || (-1 < *(int *)(DAT_00355078 * 4 + (long)item_states)))
  {
    // Circle button pressed and item is available
    DAT_00355078 = 1; // Reset selection for next time
  }
  else
  {
    // Circle button pressed but item is disabled
    item_count = -900;
  }

  return item_count;
}
