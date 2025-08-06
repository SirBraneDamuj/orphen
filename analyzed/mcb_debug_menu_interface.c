/*
 * MCB Debug Menu Interface - FUN_00268e20
 *
 * This function constructs a dynamic debug menu interface that displays MCB data
 * entries as selectable menu items. It demonstrates the internal structure of
 * MCB data and shows how the game accesses individual entries for processing.
 *
 * KEY INSIGHTS FOR MCB DATA STRUCTURE:
 *
 * 1. MENU CONSTRUCTION LOGIC:
 *    - Gets entry count: count_mcb_section_entries(section_index)
 *    - Gets data pointer: get_mcb_data_section_pointer(section_index)
 *    - Builds formatted menu strings for each entry
 *    - Creates selectable menu interface
 *
 * 2. MCB ENTRY ACCESSING PATTERN:
 *    - Entries are accessed with: entry_base + (entry_index * 0x10)
 *    - Each entry is exactly 16 bytes (0x10)
 *    - First short of entry (*psVar10) is used for menu display
 *    - This confirms our 16-byte entry structure analysis
 *
 * 3. FORMAT STRING PATTERNS:
 *    - Sections 0-13: Uses format 0x34d6d0 = "MP%02d%02d"
 *      → Creates: "MP0001", "MP0102", etc. (Map section + map ID)
 *      → MP = Map, ~10 maps per section typically
 *    - Section 14: Uses format 0x3550b8 = "BG%02d"
 *      → Creates: "BG01", "BG02", etc. (Battleground entries)
 *      → BG = Battleground, ~75 battle scenes
 *    - Header: Uses format 0x3550b0 = "MP%02d"
 *      → Creates: "MP00", "MP01", etc. (section names)
 *
 * 4. SELECTION PROCESSING:
 *    When user selects an entry:
 *    - Calculates entry offset: lVar8 * 0x10 + base_pointer - 0x10
 *    - Reads first short: *(short *)entry_address
 *    - Stores in iGpffffb280 for further processing
 *    - Sets processing flags and triggers map loading
 *
 * 5. SPECIAL HANDLING:
 *    - Section 0, Entry 99: Special exit condition
 *    - Section 12 (0xC): Special audio/scene handling
 *      - Entry 10: Sets uGpffffb662 = 0x12
 *      - Entry 11: Sets iGpffffb280 = 10, enables flags
 *
 * This function reveals that MCB entries contain map/battleground identifiers
 * and the first short of each entry is the primary identifier used
 * by the game's loading system. The MCB system organizes game maps into
 * 14 logical sections (~10 maps each) plus a battleground section (~75 scenes).
 *
 * CRITICAL ANALYSIS FOR MCB DATA STRUCTURE:
 *
 * This function reveals the exact MCB data access pattern:
 * 1. Each entry is exactly 16 bytes (accessed with * 0x10)
 * 2. First short of each entry is the primary identifier
 * 3. The data structure contains map and battleground identifiers
 * 4. Section 14 (Battleground) is special and uses different formatting
 *
 * The line: selected_entry_id = *(short *)((int)user_input * 0x10 + base - 0x10)
 * Shows exactly how to access individual MCB entries!
 *
 * MCB System Purpose:
 * - Sections 0-13: Map data (~10 maps per section = ~140 total maps)
 * - Section 14: Battleground data (~75 battle scenes)
 * - Each entry contains map/battleground configuration data
 * - First short is the primary identifier used by the loading system
 * - Remaining 14 bytes per entry contain map-specific data
 *
 * Original function: FUN_00268e20
 * Address: 0x00268e20
 */

#include "orphen_globals.h"

// Forward declarations for referenced functions
extern int count_mcb_section_entries(int section_index);       // FUN_0022a300
extern short *get_mcb_data_section_pointer(int section_index); // FUN_0022a238
extern void FUN_00267e78(void *buffer, int size);              // Memory clear/initialization
extern int FUN_0030c1d8(char *buffer, int format_addr, ...);   // sprintf_formatted
extern long FUN_002686c8(void *menu_data, int param);          // Menu input handler
extern void FUN_002686a0(void);                                // Clear controller input
extern void FUN_00267da0(void *dst, void *src, int size);      // Memory copy

// Global variables used by MCB debug menu
extern char cGpffffb663; // Debug mode state flag
extern int iGpffffb12c;  // Menu initialization flag
extern uint uGpffffbdd4; // Current MCB section index (0-14)
extern uint uGpffffb284; // Stored section index
extern int iGpffffb280;  // Selected entry ID
extern int iGpffffb108;  // Current menu selection
extern int iGpffffb288;  // Background menu selection
extern uint uGpffffae10; // Backup section index
extern int iGpffffae14;  // Backup entry ID
extern uint uGpffffb662; // Audio/scene flag
extern uint uGpffffb657; // Scene enable flag
extern uint uGpffffb66a; // Scene parameter
extern uint uGpffffb27c; // Processing flags
extern uint uGpffffb0e4; // System state
extern uint uGpffffb124; // Menu state

// Menu buffer addresses
extern char DAT_005721e8[0x400]; // Main menu text buffer (1024 bytes)
extern void *DAT_005725e8;       // Menu pointer table
extern int DAT_005725ec[];       // Menu item pointer array

int mcb_debug_menu_interface(void)
{
  bool section_underflow;
  short selected_entry_id;
  char debug_state_backup;
  int entry_count;
  short *section_data_ptr;
  int menu_text_offset;
  int format_result_length;
  long user_input;
  long validation_result;
  short *current_entry_ptr;
  int remaining_entries;
  int *menu_item_ptr;

  debug_state_backup = cGpffffb663;

  // Initialize menu if not already done
  if (iGpffffb12c == 0)
  {
    // Temporarily disable debug state during initialization
    cGpffffb663 = 0;

    // Get MCB section data
    entry_count = count_mcb_section_entries(uGpffffbdd4);
    section_data_ptr = get_mcb_data_section_pointer(uGpffffbdd4);

    // Restore debug state
    cGpffffb663 = debug_state_backup;

    // Clear menu text buffer (1024 bytes)
    FUN_00267e78(DAT_005721e8, 0x400);

    // Create section header: "MP%02d" (format at 0x3550b0)
    menu_text_offset = FUN_0030c1d8(DAT_005721e8, 0x3550b0, uGpffffbdd4);

    // Initialize menu pointer table
    DAT_005725e8 = &DAT_005721e8;
    menu_text_offset = menu_text_offset + 0x5721e9; // Move past header

    format_result_length = 0;

    // Build menu entries for each MCB entry
    if (entry_count > 0)
    {
      menu_item_ptr = &DAT_005725ec; // Menu item pointer array
      current_entry_ptr = section_data_ptr;
      remaining_entries = entry_count;

      do
      {
        // Store pointer to this menu item's text
        *menu_item_ptr = menu_text_offset;

        // Format menu text based on section type
        if (uGpffffbdd4 < 0xe)
        {
          // Sections 0-13: Use "MP%02d%02d" format (section + entry ID)
          format_result_length = FUN_0030c1d8(menu_text_offset, 0x34d6d0,
                                              uGpffffbdd4, *current_entry_ptr);
        }
        else
        {
          // Section 14: Use "BG%02d" format (background entries)
          format_result_length = FUN_0030c1d8(menu_text_offset, 0x3550b8,
                                              *current_entry_ptr);
        }

        // Move to next MCB entry (16 bytes = 8 shorts)
        current_entry_ptr += 8;

        // Advance text buffer position (+ null terminator)
        menu_text_offset = menu_text_offset + format_result_length + 1;

        remaining_entries--;
        menu_item_ptr++;
        format_result_length = entry_count; // Store total count

      } while (remaining_entries != 0);
    }

    // Store end pointer for menu system
    (&DAT_005725e8)[format_result_length + 1] = (void *)menu_text_offset;

    // Mark menu as initialized
    iGpffffb12c = 1;

    // Set initial menu selection
    if (cGpffffb663 == 0)
    {
      if (uGpffffbdd4 == uGpffffb284)
      {
        // Restore previous selection in this section
        iGpffffb108 = 0;
        if (entry_count > 0)
        {
          iGpffffb108 = 1;

          // Search for previously selected entry
          if (*section_data_ptr == (short)iGpffffb280)
          {
            goto HANDLE_INPUT;
          }

          iGpffffb108 = 1;
          while ((section_data_ptr += 8,
                  iGpffffb108 < entry_count &&
                      *section_data_ptr != (short)iGpffffb280))
          {
            iGpffffb108++;
          }
        }
        iGpffffb108++;
      }
      else
      {
        // New section, start at first item
        iGpffffb108 = 1;
      }
    }
    else
    {
      // Background menu mode
      iGpffffb108 = iGpffffb288 + 1;
    }
  }

HANDLE_INPUT:
  // Handle user input for menu navigation
  user_input = FUN_002686c8(0x5725e8, 0);
  if (user_input == 0)
    goto CLEAR_INPUT_AND_EXIT;

  if (user_input == -0x385)
  {
    // Left pressed - previous section
    section_underflow = (uGpffffbdd4 == 0);
    uGpffffbdd4--;
    if (section_underflow)
    {
      uGpffffbdd4 = 0xe; // Wrap to section 14
    }
  }
  else if (user_input == -0x386)
  {
    // Right pressed - next section
    section_underflow = (uGpffffbdd4 > 0xd);
    uGpffffbdd4++;
    if (section_underflow)
    {
      uGpffffbdd4 = 0; // Wrap to section 0
    }
  }
  else if (user_input > 0)
  {
    // Item selected - process the selection

    // Save current state
    uGpffffae10 = uGpffffb284;
    iGpffffae14 = iGpffffb280;

    if (uGpffffbdd4 < 0xe)
    {
      // Sections 0-13: Map/scene selection
      validation_result = count_mcb_section_entries(uGpffffbdd4);
      debug_state_backup = cGpffffb663;

      if (validation_result == 0)
      {
        // Empty section
        iGpffffb108 = 1;
        iGpffffb12c = 0;
        return 0;
      }

      cGpffffb663 = 0;
      format_result_length = get_mcb_data_section_pointer(uGpffffbdd4);

      // CRITICAL: Extract entry ID from selected MCB entry
      // This shows the 16-byte entry structure access pattern
      selected_entry_id = *(short *)((int)user_input * 0x10 +
                                     format_result_length - 0x10);

      // Store selection for map loading system
      uGpffffb284 = uGpffffbdd4;            // Section index
      iGpffffb280 = (int)selected_entry_id; // Entry ID

      cGpffffb663 = debug_state_backup;

      // Special handling for specific entries
      if ((uGpffffbdd4 == 0) && (selected_entry_id == 99))
      {
        goto CLEAR_INPUT_AND_EXIT; // Exit condition
      }

      if (uGpffffbdd4 == 0xc)
      {
        // Section 12: Special audio/scene handling
        if (selected_entry_id == 10)
        {
          uGpffffb662 = 0x12; // Set audio flag
        }
        else if (selected_entry_id == 0xb)
        {
          iGpffffb280 = 10; // Override entry ID
          uGpffffb657 = 1;  // Enable scene flag
          uGpffffb66a = 0;  // Clear scene parameter
        }
        else
        {
          uGpffffb27c = 0; // Clear processing flags
          goto SET_PROCESSING_FLAGS;
        }
        uGpffffb27c = 0;
      }
      else
      {
        uGpffffb27c = 0;
      }
    }
    else
    {
      // Section 14: Background selection
      if (user_input == 1)
      {
        // First background entry selected
        iGpffffb108 = 1;
        iGpffffb12c = 0;
        return 0;
      }

      uGpffffb27c = 0x20000;             // Background processing flag
      iGpffffb288 = (int)user_input - 1; // Store background index

      // Copy background data (12 bytes from 0x58bed0 to 0x31e668)
      FUN_00267da0(0x31e668, 0x58bed0, 0xc);
    }

  SET_PROCESSING_FLAGS:
    // Set final processing flags and trigger map loading
    uGpffffb0e4 = 0;                    // Clear system state
    iGpffffb108 = 1;                    // Reset menu selection
    uGpffffb124 = 0;                    // Clear menu state
    iGpffffb12c = 0;                    // Mark menu for reinitialization
    uGpffffb27c = uGpffffb27c | 0x2001; // Set processing flags
    return 0;
  }
  else
  {
    goto CLEAR_INPUT_AND_EXIT;
  }

  // Section changed - reinitialize menu
  iGpffffb12c = 0;

CLEAR_INPUT_AND_EXIT:
  FUN_002686a0();    // Clear controller input state
  return 0xfffffc7c; // Continue menu operation
}
