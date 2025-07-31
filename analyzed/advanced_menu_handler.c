#include "orphen_globals.h"

// Forward declarations for referenced functions
extern uint *get_text_resource(int text_index);                                                       // Get text resource by index (FUN_0025b9e8)
extern short calculate_text_width(char *text_string, int scale);                                      // Calculate text width (FUN_00238e68)
extern void render_text_with_scaling(int x, int y, char *text, uint color, int scale_x, int scale_y); // Render text with scaling (FUN_00238608)
extern unsigned int read_controller_input(unsigned short button_mask, long enable_sticky_input);      // Controller input reader with repeat handling (FUN_0023b9f8)
extern void swap_action_mapping(char param1, char param2);                                            // Swap action mapping values in controller button mapping table (FUN_00232058)
extern void play_menu_sound(void);                                                                    // Play menu navigation sound effect (FUN_002256c0)
extern void play_menu_back_sound(void);                                                               // Play menu back/cancel sound effect (FUN_002256b0)
extern void process_audio_data(unsigned long audio_data);                                             // Process audio data and generate sound pattern (FUN_0023b8e0)
extern void render_menu_item(long slot, unsigned long text_id);                                       // Render menu item with text and visual elements (FUN_00231e60)

/**
 * Button Configuration/Remapping Menu Handler
 *
 * This function manages the in-game button configuration menu that allows players
 * to remap controller actions. The menu has multiple operational states:
 * - State 0: Initialize menu layout and calculate text widths for 5 menu items (text IDs 0x50-0x54)
 * - State 1: Handle upward scrolling/animation with position updates
 * - State 2: Active menu state with controller input processing and text rendering
 * - State 3: Handle downward scrolling/animation with boundary checking
 *
 * The menu supports:
 * - Dynamic text width calculation and layout
 * - Controller input for navigation (D-pad) and selection
 * - Visual feedback and audio cues
 * - Button remapping via swap_action_mapping() function
 * - Text rendering for instructions and menu items
 * - Scroll boundaries and error handling
 *
 * Menu items correspond to game actions:
 * - Spell (Triangle button)
 * - Weapon Attack (Circle button)
 * - Talk Action (X button)
 * - Jump (Square button)
 * - Exit (finalize configuration)
 *
 * Text rendering includes instructional text (IDs 0x55, 0x56) and menu items (0x50-0x54).
 * Controller mapping uses button flags (0x10, 0x20, 0x40, 0x80) for different actions.
 * Each menu selection corresponds to a game action that can be remapped to different buttons.
 *
 * Original function: FUN_002320a8
 * Address: 0x002320a8
 *
 * @param menu_state Current state of the menu system (0=init, 1=scroll_up, 2=active, 3=scroll_down)
 * @return New menu state after processing
 */
int advanced_menu_handler(int menu_state)
{
  undefined1 action_value;
  int text_width;
  undefined8 text_resource;
  long input_flags;
  int *position_ptr;
  int loop_counter;
  int y_position;
  long max_text_width;

  if (menu_state == 0)
  {
    // State 0: Initialize menu layout
    max_text_width = 0;
    y_position = 0;
    menu_scroll_limit = 0x4b;                               // Set scroll boundary
    menu_base_y_position = menu_current_y_position + -0x1e; // Calculate base position

    // Calculate maximum text width for 5 menu items (0x50-0x54)
    do
    {
      text_resource = (undefined8)get_text_resource(y_position + 0x50); // Get menu item text
      y_position = y_position + 1;
      text_width = calculate_text_width((char *)text_resource, 0x14); // Calculate width
      if (max_text_width < text_width)
      {
        max_text_width = text_width; // Track maximum width
      }
    } while (y_position < 5);

    menu_calculated_width = (int)max_text_width + 0x20; // Add padding
    menu_state = 1;                                     // Transition to scroll state
    current_menu_selection = 4;                         // Set initial selection
  }
  else if (menu_state == 1)
  {
    // State 1: Handle upward scrolling animation
    y_position = controller_y_input + 3;
    if (-1 < controller_y_input)
    {
      y_position = controller_y_input;
    }
    position_ptr = &menu_current_y_position;
    loop_counter = 0;
    text_width = y_position >> 2; // Calculate scroll speed
    if (menu_scroll_limit < y_position >> 2)
    {
      text_width = menu_scroll_limit; // Clamp to limit
    }

    // Update Y positions for menu items (appears to be 7 items, 6-element structure each)
    do
    {
      loop_counter = loop_counter + -1;
      *position_ptr = *position_ptr + text_width; // Move up
      position_ptr = position_ptr + 6;            // Next menu item (6 elements each)
    } while (-1 < loop_counter);

    // Update positions for another set of items (5 items)
    y_position = 5;
    position_ptr = &menu_item_y_positions;
    do
    {
      y_position = y_position + -1;
      *position_ptr = *position_ptr - text_width; // Move down
      position_ptr = position_ptr + 6;            // Next item
    } while (-1 < y_position);

    menu_scroll_limit = menu_scroll_limit - text_width; // Update scroll limit
    if (menu_scroll_limit < 1)
    {
      menu_state = 2; // Transition to active state
    }
  }
  else if (menu_state == 3)
  {
    // State 3: Handle downward scrolling animation
    y_position = controller_y_input + 3;
    if (-1 < controller_y_input)
    {
      y_position = controller_y_input;
    }
    y_position = y_position >> 2; // Calculate scroll speed

    // Check scroll boundary
    if (menu_current_y_position - menu_item_y_positions < 0x1f)
    {
      process_audio_data(0x355608); // Process boundary error audio
      return 0;                     // Exit on boundary
    }

    // Update positions for downward scroll
    position_ptr = &menu_item_y_positions;
    text_width = 5;
    do
    {
      text_width = text_width + -1;
      *position_ptr = *position_ptr + y_position; // Move down
      position_ptr = position_ptr + 6;
    } while (-1 < text_width);

    // Update main menu positions
    text_width = 0;
    position_ptr = &menu_current_y_position;
    do
    {
      text_width = text_width + -1;
      *position_ptr = *position_ptr - y_position; // Move up
      position_ptr = position_ptr + 6;
    } while (-1 < text_width);

    // Reset positions if too close
    position_ptr = &menu_current_y_position;
    if ((menu_current_y_position - menu_item_y_positions) - y_position < 0x1e)
    {
      text_width = 6;
      y_position = menu_base_y_position + 0x1e;
      do
      {
        *position_ptr = y_position; // Reset to base positions
        text_width = text_width + -1;
        position_ptr = position_ptr + 6;
        y_position = y_position + -0x1e; // 30-pixel spacing
      } while (-1 < text_width);
    }
  }
  else
  {
    if (menu_state != 2)
    {
      return menu_state; // Unknown state, return unchanged
    }

    // State 2: Active menu processing
    input_flags = read_controller_input(0x5000, 1); // Check for input with button mask 0x5000
    if (input_flags != 0)
    {
      // Handle D-pad navigation
      if ((controller_1_input & 0x1000) == 0) // Up not pressed
      {
        if (((controller_1_input & 0x4000) != 0) && // Down pressed
            (current_menu_selection = current_menu_selection + 1, 4 < current_menu_selection))
        {
          current_menu_selection = 0; // Wrap to top
        }
      }
      else
      {
        current_menu_selection = current_menu_selection + -1; // Move up
        if (current_menu_selection < 0)
        {
          current_menu_selection = 4; // Wrap to bottom
        }
      }
      play_menu_sound(); // Audio feedback
    }

    // Handle menu item selection
    if (current_menu_selection < 4)
    {
      if ((controller_2_input & 0xf0) != 0) // Action buttons pressed
      {
        action_value = menu_action_mapping[current_menu_selection]; // Get action for current selection

        // Process different button types
        if ((controller_2_input & 0x10) == 0) // Button 0x10 not pressed
        {
          if ((controller_2_input & 0x20) == 0) // Button 0x20 not pressed
          {
            if ((controller_2_input & 0x40) == 0) // Button 0x40 not pressed
            {
              if ((controller_2_input & 0x80) != 0) // Button 0x80 pressed
              {
                swap_action_mapping(action_value, 0x80); // Execute action with flag
              }
            }
            else
            {
              swap_action_mapping(action_value, 0x40); // Execute action with flag
            }
          }
          else
          {
            swap_action_mapping(action_value, 0x20); // Execute action with flag
          }
        }
        else
        {
          swap_action_mapping(action_value, 0x10); // Execute action with flag
        }
        play_menu_sound(); // Audio feedback
      }
    }
    else if ((controller_2_input & 0x40) != 0) // Special case for selection 4+
    {
      current_menu_selection = 0; // Reset selection
      play_menu_back_sound();     // Play back/cancel sound
      return 3;                   // Transition to scroll down state
    }
  }

  y_position = menu_current_y_position;
  if (menu_state == 2)
  {
    // Render instructional text in active state
    text_resource = (undefined8)get_text_resource(0x55); // Get instruction text 1
    text_width = calculate_text_width((char *)text_resource, 0x14);
    render_text_with_scaling(0x130 - text_width, y_position + -0x78, (char *)text_resource,
                             0xffffffff80808080, 0x14, 0x16); // Right-aligned text

    text_resource = (undefined8)get_text_resource(0x56); // Get instruction text 2
    text_width = calculate_text_width((char *)text_resource, 0x14);
    render_text_with_scaling(0x130 - text_width, y_position + -0x8e, (char *)text_resource,
                             0xffffffff80808080, 0x14, 0x16); // Right-aligned text

    // Render menu items
    render_menu_item(4, 0x50); // Render menu item 0
    render_menu_item(5, 0x52); // Render menu item 1
    render_menu_item(6, 0x51); // Render menu item 2
    render_menu_item(7, 0x53); // Render menu item 3
    render_menu_item(8, 0x54); // Render menu item 4
  }
  return menu_state;
}

// Global variables for advanced menu system:

/**
 * Menu scroll limit for animation boundaries
 * Original: DAT_0031c514
 */
extern int menu_scroll_limit;

/**
 * Menu base Y position reference
 * Original: DAT_0031c510
 */
extern int menu_base_y_position;

/**
 * Menu calculated width from text measurements
 * Original: DAT_0031c504
 */
extern int menu_calculated_width;

/**
 * Current menu Y position
 * Original: DAT_0031c468
 */
extern int menu_current_y_position;

/**
 * Menu item Y positions array
 * Original: DAT_0031c480
 */
extern int menu_item_y_positions;

/**
 * Current menu selection index (0-4)
 * Original: DAT_00355c28
 */
extern int current_menu_selection;

/**
 * Controller Y input value
 * Original: DAT_003555bc
 */
extern int controller_y_input;

/**
 * Controller 1 input state
 * Original: DAT_003555f4
 */
extern int controller_1_input;

/**
 * Controller 2 input state
 * Original: DAT_003555f6
 */
extern int controller_2_input;

/**
 * Menu action mapping array (maps selection to action ID)
 * Original: DAT_0035560c
 */
extern unsigned char menu_action_mapping[];
