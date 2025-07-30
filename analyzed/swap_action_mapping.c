#include "orphen_globals.h"

/**
 * Swap action mapping values in controller button mapping table
 *
 * This function swaps two values in the action mapping table based on
 * matching parameters. It's used in menu systems to remap controller
 * button actions dynamically.
 *
 * The function iterates through an 8-byte action mapping table and:
 * - If it finds param2 in the table, replaces it with param1
 * - If it finds param1 in the table, replaces it with param2
 * - Only performs swaps if param1 != param2
 *
 * This allows dynamic remapping of controller button actions in menus,
 * such as swapping which button performs which action based on context.
 *
 * Original function: FUN_00232058
 * Address: 0x00232058
 *
 * @param param1 First value to swap (typically action ID)
 * @param param2 Second value to swap (typically button flag)
 */
void swap_action_mapping(char param1, char param2)
{
  char current_value;
  char *mapping_ptr;

  // Only perform swap if parameters are different
  if (param1 != param2)
  {
    // Start at beginning of action mapping table
    mapping_ptr = &action_mapping_table;
    current_value = action_mapping_table;

    // Iterate through entire 8-byte mapping table
    while (true)
    {
      // If current value matches param2, replace with param1
      if (current_value == param2)
      {
        *mapping_ptr = param1;
      }
      // If current value matches param1, replace with param2
      else if (current_value == param1)
      {
        *mapping_ptr = param2;
      }

      // Move to next byte in table
      mapping_ptr = mapping_ptr + 1;

      // Check if we've reached end of table (address 0x35560f)
      if (0x35560f < (int)mapping_ptr)
        break;

      // Load next value
      current_value = *mapping_ptr;
    }
  }

  return;
}

// Global variables for action mapping system:

/**
 * Action mapping table for controller button remapping
 * 8-byte table mapping controller buttons to actions
 * Original: DAT_00355608
 * Address range: 0x355608 - 0x35560f
 */
extern char action_mapping_table;
