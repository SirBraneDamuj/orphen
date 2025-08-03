/*
 * Comprehensive Debug Menu Handler - FUN_00269140
 *
 * This is the main debug menu system that provides access to extensive debugging
 * and testing functionality. This menu includes the MAP SELECT option and many
 * other advanced debug features that were available to developers.
 *
 * Based on the data structure shown in Ghidra at 0031e780, this menu includes:
 * - DEBUG MENU 1 (title)
 * - MAP SELECT (map loading/testing functionality)
 * - BGM CHECK (background music testing)
 * - VOICE CHECK (voice audio testing)
 * - S.E CHECK (sound effects testing)
 * - VIBE CHECK (controller vibration testing)
 * - CAMERA DISTANCE (camera debugging)
 * - JUMP TEST (movement testing)
 * - ROUTE CHECK (path/navigation testing)
 * - And many other debugging options
 *
 * This represents the complete debug menu system that supplements the smaller
 * 3-option debug menu (FUN_00268d30) analyzed earlier.
 *
 * The function manages:
 * - Menu category navigation (DAT_003550c0)
 * - Submenu item selection (DAT_003550c2)
 * - Menu state tracking (DAT_003550bf)
 * - Visual rendering and input processing
 *
 * Original function: FUN_00269140
 * Address: 0x00269140
 */

#include "orphen_globals.h"

// Forward declarations for referenced functions
extern void FUN_00268498(void *text_ptr, int x, int y);             // render_menu_text
extern void FUN_0030c1d8(char *buffer, int format_addr, int value); // sprintf_formatted
extern void FUN_00268650(int x, int y, int w, int h, int color);    // render_menu_rectangle
extern long FUN_0023b9f8(int input_mask, int param);                // check_controller_input
extern void FUN_002686a0(void);                                     // clear_controller_input_state
extern void FUN_00205f98(int param1, int param2);                   // Execute debug function/test

// Global variables for comprehensive debug menu (not yet in orphen_globals.h)
extern unsigned char DAT_003550c0;  // Current debug menu category index (0-7)
extern unsigned short DAT_003550c2; // Current submenu item index
extern unsigned char DAT_003550bf;  // Menu navigation state (0=category, 1=item)
extern unsigned int DAT_00354d24;   // Menu return/action state
extern unsigned short DAT_00354c00; // Menu item count array (indexed by category)

// Controller input state variables
extern unsigned short DAT_003555f4; // Controller 1 input state
extern unsigned short DAT_003555f6; // Controller 2 input state

/**
 * Comprehensive debug menu handler with MAP SELECT and advanced options
 *
 * This function provides the main debug menu interface with multiple categories
 * of debugging tools. The menu operates in two modes:
 * - Category selection mode: Choose main debug category (0-7)
 * - Item selection mode: Choose specific debug function within category
 *
 * Key debug categories include:
 * - MAP SELECT: Map loading and testing functionality
 * - BGM CHECK: Background music testing
 * - VOICE CHECK: Voice audio testing
 * - EFFECT/S.E. CHECK: Sound effects testing
 * - CAMERA controls: Camera debugging tools
 * - MOVEMENT tests: Jump, route, and navigation testing
 * - And other advanced debugging features
 *
 * Navigation:
 * - Up/Down D-pad: Navigate categories or items
 * - Left/Right D-pad: Switch between category/item selection modes
 * - Triangle/X: Navigate by ±10 in item mode
 * - Circle/Square/Triangle/X: Execute selected debug function
 * - Various button combinations trigger different debug operations
 *
 * Returns:
 *   int - Menu state/action result
 */
int comprehensive_debug_menu_handler(void)
{
  int category_limit;
  int controller_input;
  int navigation_delta;
  int menu_category;
  int submenu_item;
  char display_buffer[128];

  // Determine category limit (max 2 if current category < 3, otherwise use current)
  category_limit = 2;
  if ((int)(char)DAT_003550c0 < 3)
  {
    category_limit = (int)(char)DAT_003550c0;
  }

  // Render main debug menu title
  FUN_00268498(0x34d528, -48, 0x44); // "BGM CHECK" or main title

  // Format and display current category index
  FUN_0030c1d8(display_buffer, 0x34d6f0, DAT_003550c0);
  FUN_00268498(display_buffer, -48, 0x30);

  // Format and display current item index
  FUN_0030c1d8(display_buffer, 0x34d700, DAT_003550c2);
  FUN_00268498(display_buffer, -48, 0x1c);

  // Render additional menu text elements
  FUN_00268498(0x34d710, -48, 8);
  FUN_00268498(0x34d720, -48, -12);
  FUN_00268498(0x34d730, -48, -32);
  FUN_00268498(0x34d740, -48, -52);

  // Render menu borders and highlight
  FUN_00268650(-52, 0x44, 0x98, 0x14, 0x6000);                      // Top border
  FUN_00268650(-52, DAT_003550bf * -0x14 + 0x30, 0x98, 0x14, 0x60); // Selection highlight
  FUN_00268650(-52, 0x30, 0x98, 0x78, 0x600000);                    // Main border

  // Process controller input
  controller_input = FUN_0023b9f8(0xf00c, 0);

  if (controller_input != 0)
  {
    // Handle D-pad navigation
    if ((DAT_003555f4 & 0x1000) == 0)
    { // Up not pressed
      if ((DAT_003555f4 & 0x4000) == 0)
      { // Down not pressed
        if ((DAT_003555f4 & 0x8000) == 0)
        { // Left not pressed
          if ((DAT_003555f4 & 0x2000) == 0)
          { // Right not pressed
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
            navigation_delta = 1; // Right: increase by 1
          }
        }
        else
        {
          navigation_delta = -1; // Left: decrease by 1
        }

        // Apply navigation in item selection mode
        if (DAT_003550bf == '\0')
        {
          // Category selection mode
          menu_category = (unsigned int)DAT_003550c0;
          DAT_003550c0 = (unsigned char)(menu_category + navigation_delta);
          navigation_delta = (int)((menu_category + navigation_delta) * 0x1000000) >> 0x18;

          if (navigation_delta < 0)
          {
            DAT_003550c0 = 7; // Wrap to category 7
          }
          else if (7 < navigation_delta)
          {
            DAT_003550c0 = 0; // Wrap to category 0
          }

          // Adjust submenu item if it exceeds the new category's limit
          navigation_delta = 2;
          if ((char)DAT_003550c0 < '\x03')
          {
            navigation_delta = (int)(char)DAT_003550c0;
          }

          if (*(short *)(&DAT_00354c00 + navigation_delta * 2) <= (short)DAT_003550c2)
          {
            DAT_003550c2 = *(short *)(&DAT_00354c00 + navigation_delta * 2) - 1;
          }
        }
        else
        {
          // Item selection mode - navigate within current category
          submenu_item = (int)(((unsigned int)DAT_003550c2 + navigation_delta) * 0x10000) >> 0x10;
          DAT_003550c2 = (unsigned short)((unsigned int)DAT_003550c2 + navigation_delta);

          if (submenu_item < 0)
          {
            // Wrap to last item in category
            DAT_003550c2 = *(short *)(&DAT_00354c00 + (int)category_limit * 2) - 1;
          }
          else if (*(short *)(&DAT_00354c00 + (int)category_limit * 2) <= submenu_item)
          {
            DAT_003550c2 = 0; // Wrap to first item
          }
        }
      }
      else if (DAT_003550bf < '\x01')
      {
        // Down pressed in category mode - switch to item mode
        DAT_003550bf = DAT_003550bf + '\x01';
      }
      else
      {
        // Down pressed in item mode - stay in item mode
        DAT_003550bf = '\0';
      }
    }
    else if (DAT_003550bf < '\x01')
    {
      // Up pressed in category mode - switch to item mode
      DAT_003550bf = '\x01';
    }
    else
    {
      // Up pressed in item mode - switch to category mode
      DAT_003550bf = DAT_003550bf - 1;
    }
  }

  // Set return state for processing
  DAT_00354d24 = 0xfffffffe;

  // Handle action buttons for debug function execution
  if ((DAT_003555f6 & 0x20) == 0)
  { // Circle not pressed
    if ((DAT_003555f6 & 0x40) == 0)
    { // Square not pressed
      if ((DAT_003555f6 & 0x80) == 0)
      { // Triangle not pressed
        if ((DAT_003555f6 & 0x10) == 0)
        {
          // No action button pressed - continue menu
          goto MENU_CONTINUE;
        }
        // X button pressed - execute debug function
        FUN_00205f98(DAT_003550c0, 0);
      }
    }
  }

  // Additional action button handling and debug function execution
  // ... (rest of function continues with specific debug implementations)

MENU_CONTINUE:
  // Clear controller input and continue
  FUN_002686a0();

  return DAT_00354d24;
}

// Global variables for comprehensive debug menu system:

/**
 * Debug menu category index (0-7)
 * Controls which main debug category is selected:
 * - 0: MAP SELECT and map-related debugging
 * - 1: BGM CHECK and audio debugging
 * - 2: VOICE CHECK and voice testing
 * - 3: EFFECT/S.E. CHECK and sound effects
 * - 4: CAMERA controls and visual debugging
 * - 5: MOVEMENT testing (JUMP, ROUTE, etc.)
 * - 6-7: Additional debug categories
 * Original: DAT_003550c0
 */
extern unsigned char debug_menu_category;

/**
 * Debug submenu item index
 * Controls which specific item within the current category is selected
 * Range varies by category (stored in DAT_00354c00 array)
 * Original: DAT_003550c2
 */
extern unsigned short debug_submenu_item;

/**
 * Debug menu navigation state
 * 0 = Category selection mode (navigating main categories)
 * 1 = Item selection mode (navigating items within category)
 * Original: DAT_003550bf
 */
extern unsigned char debug_menu_state;

/**
 * Menu item count array
 * Array indexed by category that stores the number of items in each category
 * Used to determine navigation limits and wraparound behavior
 * Original: DAT_00354c00
 */
extern unsigned short debug_category_item_counts[];

/**
 * Menu action/return state
 * Used to track menu state and return values for debug function execution
 * Original: DAT_00354d24
 */
extern unsigned int debug_menu_action_state;
