#include "orphen_globals.h"

/**
 * Map menu item ID to display index
 *
 * This function maps various menu item IDs to their corresponding display indices.
 * It appears to handle menu organization by converting logical menu item IDs
 * to their visual positions in a menu layout.
 *
 * The mapping suggests this could be for an in-game menu system with:
 * - Items 1, 3-7: Regular menu items (mapped to indices 0-5)
 * - Item 0x16 (22): Special menu item (mapped to index 6)
 * - Default case: Invalid/unavailable items (mapped to index 7)
 *
 * Original function: FUN_002298d0
 * Address: 0x002298d0
 *
 * @param menu_item_id The logical menu item identifier
 * @return The display index for the menu item (0-6 for valid items, 7 for invalid)
 */
undefined4 map_menu_item_to_index(undefined4 menu_item_id)
{
  switch (menu_item_id)
  {
  case 1:
    return 0;
  default:
    return 7; // Invalid/unavailable item
  case 3:
    return 1;
  case 4:
    return 2;
  case 5:
    return 3;
  case 6:
    return 4;
  case 7:
    return 5;
  case 0x16: // Item 22
    return 6;
  }
}
