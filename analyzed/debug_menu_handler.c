/*
 * Debug Menu Handler - FUN_00268d30
 *
 * Manages the debug menu system that allows toggling various debug display options.
 * This function handles the display of debug menu items and processes user input
 * to navigate and select debug options.
 *
 * Debug display options controlled:
 * - POSITION_DISP: Shows position information on screen
 * - MINI_MAP_DISP: Shows mini-map display
 * - SCR_SUBPROC_DISP: Non-functional debug option (no implementation found)
 *
 * The function sets up text strings to show "ON " or "OFF" status for each option
 * based on flag bits in global variables.
 *
 * Original function: FUN_00268d30
 */

#include "orphen_globals.h"

// Forward declarations for analyzed functions
extern void set_debug_option_text(unsigned char *text_buffer, long condition);     // FUN_00268c98
extern int process_menu_input_and_navigation(void **menu_items, int *item_states); // FUN_002686c8
extern void clear_controller_input_state(void);                                    // FUN_002686a0

/*
 * Handles the debug menu display and input processing
 *
 * Sets up debug option text displays based on current flag states:
 * - Position display toggle (controlled by uGpffffb128)
 * - Mini-map display toggle (controlled by bGpffffb66d bit 2)
 * - SCR_SUBPROC_DISP toggle (controlled by bGpffffb66d bit 7, but appears non-functional)
 *
 * Manages menu navigation state and processes user input for selection.
 *
 * Returns:
 *   long - Menu selection result:
 *          > 0: Selected menu item index
 *          0: No selection made
 *          < 0: Menu cancelled or error
 */
long debug_menu_handler(void)
{
  long menu_result;

  // Set debug option display strings based on current state
  // Position display - shows "ON " if enabled, "OFF" if disabled
  set_debug_option_text(PTR_s_ON__POSITION_DISP_0031e7ac, uGpffffb128);

  // Mini-map display - controlled by bit 2 of flag byte
  set_debug_option_text(PTR_s_ON__MINI_MAP_DISP_0031e7b0, bGpffffb66d & 4);

  // SCR_SUBPROC_DISP - controlled by bit 7 of flag byte (appears non-functional)
  set_debug_option_text(PTR_s_ON__SCR_SUBPROC_DISP_0031e7a8, bGpffffb66d & 0x80);

  // Set up menu display colors/highlighting based on debug mode state
  if (cGpffffb663 == '\0')
  {
    // Not in debug mode - set normal menu colors
    DAT_0031e84c = 0x15;       // Menu item color (normal)
    DAT_0031e858 = 0xffffffff; // Disabled/grayed color
  }
  else
  {
    // In debug mode - set debug menu colors
    DAT_0031e84c = 0xffffffff; // Menu item color (debug)
    DAT_0031e858 = 0x18;       // Highlight color
  }

  // Initialize menu state
  uGpffffbdd8 = 1;           // Menu active flag
  uGpffffbdd0 = uGpffffb124; // Save previous menu state

  // Process menu input and get user selection
  // Menu spans from address 0x31e780 to 0x31e7f8
  menu_result = process_menu_input_and_navigation((void **)0x31e780, (int *)0x31e7f8);

  if ((menu_result != 0) && (0 < menu_result))
  {
    // Valid menu selection made
    uGpffffb124 = (unsigned int)menu_result; // Store selected item
    uGpffffb11c = 0xffffffff;                // Mark selection processed

    if (menu_result == 1)
    {
      // First menu item selected - handle special case
      uGpffffbdd4 = 0xe; // Set default action value

      if (cGpffffb663 == '\0')
      {
        // Not in debug mode - use saved action value
        uGpffffbdd4 = uGpffffb284;
      }

      uGpffffb12c = 0; // Reset action counter
    }
  }

  // Clear input state before returning
  clear_controller_input_state();

  return menu_result;
}
