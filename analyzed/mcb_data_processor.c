/*
 * MCB Data Processor - FUN_0022b300
 *
 * Processes MCB data that has already been loaded into memory. While there are only
 * two MCB files on disc (MCB0.BIN, MCB1.BIN), this function processes 15 different
 * data sets (indices 0-14), suggesting the MCB files are parsed/split into multiple
 * logical data sections after being loaded from disc.
 *
 * The function operates in two modes:
 * 1. When param_1 == 0: Process MCB data sections and output debug information
 * 2. When param_1 != 0: Reset MCB processing state
 *
 * The debug output suggests these data sections contain some type of game data,
 * with index 14 being special and referenced as "MCB[BG%03d]" in debug output.
 *
 * Global Variables Used:
 * - DAT_00355bd0: Current data section index (0-14, where 14 seems to be special)
 * - DAT_00355bd4: Number of remaining entries in current data section
 * - DAT_00355bd8: Pointer to current data entry (16-byte entries)
 * - DAT_003551f8: Special counter for data section 14 (background index)
 * - DAT_003551f4: Current map/background ID
 * - DAT_003551f0: Current entry value
 * - DAT_003551ec: Processing flags (0x2001 for normal, 0x20000 for special)
 * - DAT_00315b04: Large data array (800 bytes per entry, 8 bytes per sub-entry)
 *
 * Processing Logic:
 * 1. Iterates through data sections 0-14 sequentially
 * 2. For each section, calls FUN_0022a300 to get entry count
 * 3. Calls FUN_0022a238 to get data pointer for the section
 * 4. Processes each 16-byte entry in the data section
 * 5. Skips entries where first value > 1999 or third value is 0
 * 6. Checks if corresponding slot in data array is available
 * 7. Outputs debug information about processed entries
 * 8. Data section 14 gets special handling with different debug output format
 *
 * Debug Output Strings:
 * - "MCB all make end." - Displayed when data section limit (14) is exceeded
 * - "%02d%02d,MCB[BG%03d],maph=%d,num=%d\n" - For data section 14 entries
 * - "%02d%02d,maph=%d,num=%d\n" - For other data section entries
 * - "MCB[BG%03d]\n" - Final summary for data section 14
 *
 * The function appears to process MCB data that has been loaded and parsed from
 * the disc files (MCB0.BIN, MCB1.BIN) into 15 logical data sections. Data section 14
 * seems to be special background data that gets different processing and debug output.
 *
 * Note: This function processes data structures in memory, not direct file I/O.
 * The actual MCB file loading likely happens elsewhere in the codebase.
 */

#include "orphen_globals.h"

// Forward declarations for unanalyzed functions
extern int count_mcb_section_entries(int section_index);       // Returns entry count for MCB data section (FUN_0022a300)
extern short *get_mcb_data_section_pointer(int section_index); // Returns pointer to MCB data section (FUN_0022a238)
extern void FUN_0026c088(void *format_string, ...);            // Debug output formatter
extern void FUN_0022a1f8(void);                                // MCB cleanup function
extern void FUN_00206640(int param);                           // Display system reset

// Global variables (keeping original DAT_ names)
extern int DAT_00355bd0;    // Current MCB data section index (0-14)
extern int DAT_00355bd4;    // Number of remaining entries in current section
extern short *DAT_00355bd8; // Pointer to current MCB data entry
extern int DAT_003551f8;    // Special counter for section 14
extern int DAT_003551f4;    // Current map/ID value
extern int DAT_003551f0;    // Current entry value
extern int DAT_003551ec;    // Processing flags
extern int DAT_00315b04[];  // Large data array for MCB processing
extern int DAT_00355054;    // Global flag 1
extern int DAT_00354d2c;    // System state
extern char DAT_003555c7;   // Audio flag

// MCB data structure constants
#define MCB_DATA_SECTIONS 15    // Data sections 0-14 (parsed from MCB0/MCB1 files)
#define SPECIAL_DATA_SECTION 14 // Special background data section
#define MAX_ENTRY_VALUE 1999    // Skip entries above this value
#define DATA_ARRAY_STRIDE 800   // Bytes per map entry
#define DATA_SUBENTRY_SIZE 8    // Bytes per data sub-entry
#define MCB_ENTRY_SIZE 16       // Bytes per MCB entry (8 shorts)

// Processing flags
#define FLAG_NORMAL_PROCESSING 0x2001   // Normal data section processing
#define FLAG_SPECIAL_PROCESSING 0x20000 // Special data section 14 processing

void FUN_0022b300(long reset_flag) // MCB Data Processor
{
  short entry_value;
  int map_id;
  int entry_index;

  if (reset_flag == 0)
  {
    // Main MCB processing loop
    while (true)
    {
      do
      {
        // Check if current data section has remaining entries
        if (DAT_00355bd4 < 1)
        {
          // Move to next data section
          DAT_00355bd0++;

          // Check if we've exceeded the data section limit
          if (DAT_00355bd0 > 14)
          {
            FUN_0026c088(0x34c0e8); // "MCB all make end." message
            // Infinite loop - seems like an error condition
            while (true)
            {
              // Intentional infinite loop
            }
          }

          // Load new data section
          DAT_00355bd4 = count_mcb_section_entries(DAT_00355bd0);
          DAT_00355bd8 = get_mcb_data_section_pointer(DAT_00355bd0);

          // Special handling for data section 14
          if (DAT_00355bd0 == SPECIAL_DATA_SECTION)
          {
            DAT_003551f8 = 0; // Reset background index
            DAT_003551f4 = 1; // Set map ID
          }
        }
        else
        {
          // Move to next entry in current data section
          DAT_00355bd8 += 8; // Advance by 16 bytes (8 shorts)

          if (DAT_00355bd0 == SPECIAL_DATA_SECTION)
          {
            DAT_003551f8++; // Increment background index
          }
        }

        DAT_00355bd4--; // Decrement remaining entries

        // Set processing parameters based on data section type
        if (DAT_00355bd0 < SPECIAL_DATA_SECTION)
        {
          DAT_003551f4 = DAT_00355bd0; // Map ID = section index
          DAT_003551ec = FLAG_NORMAL_PROCESSING;
          DAT_003551f0 = (int)*DAT_00355bd8; // Entry value
        }
        else
        {
          DAT_003551ec = FLAG_SPECIAL_PROCESSING;
        }

      } while ((DAT_00355bd8[3] == 0) ||
               (entry_value = *DAT_00355bd8, entry_value > MAX_ENTRY_VALUE));

      // Determine map ID and entry index for data array lookup
      entry_index = DAT_003551f4;
      map_id = DAT_003551f0;

      if (DAT_00355bd0 == SPECIAL_DATA_SECTION)
      {
        entry_index = SPECIAL_DATA_SECTION;
        map_id = DAT_003551f8; // Use background index
      }

      // Check if data slot is available (non-zero means occupied)
      if (DAT_00315b04[map_id * (DATA_SUBENTRY_SIZE / 4) +
                       entry_index * (DATA_ARRAY_STRIDE / 4)] == 0)
      {
        break; // Found available slot
      }

      // Output debug information about occupied slot
      if (DAT_00355bd0 == SPECIAL_DATA_SECTION)
      {
        FUN_0026c088(0x34c110, entry_index, map_id, entry_value,
                     SPECIAL_DATA_SECTION, DAT_00355bd4);
      }
      else
      {
        FUN_0026c088(0x34c138);
      }
    }

    // Output final summary for special data section
    if (DAT_00355bd0 == SPECIAL_DATA_SECTION)
    {
      FUN_0026c088(0x34c158, entry_value);
    }

    // Cleanup and finalization
    FUN_0022a1f8();   // MCB cleanup
    FUN_00206640(0);  // Reset display system
    DAT_00355054 = 0; // Reset global flag 1
    DAT_00354d2c = 0; // Reset system state
    DAT_003555c7 = 0; // Reset audio flag
  }
  else
  {
    // Reset MCB processing state
    DAT_00355bd0 = 0;
    DAT_00355bd4 = 0;
  }
}

/*
 * Helper Functions (referenced but not fully analyzed):
 * - count_mcb_section_entries(section_index): Returns number of entries in MCB data section (analyzed)
 * - get_mcb_data_section_pointer(section_index): Returns pointer to MCB data section (analyzed)
 * - FUN_0026c088(...): Debug output formatter (outputs formatted debug messages)
 * - FUN_0022a1f8(): MCB cleanup function
 * - FUN_00206640(flag): Display/graphics system reset function
 */
