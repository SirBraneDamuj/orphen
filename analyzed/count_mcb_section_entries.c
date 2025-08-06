/*
 * MCB Data Section Entry Counter - FUN_0022a300
 *
 * Counts the number of entries in a specific MCB data section by iterating
 * through the data structure until it finds a zero terminator. This function
 * is essential for MCB data processing as it determines how many entries
 * need to be processed in each section.
 *
 * The function works by:
 * 1. Getting a pointer to the MCB data section using get_mcb_data_section_pointer()
 * 2. Reading 16-byte entries (8 shorts each) sequentially
 * 3. Checking the first short of each entry for zero terminator
 * 4. Counting non-zero entries until terminator is found
 *
 * MCB Entry Structure (16 bytes each):
 * - Entry[0]: First short - used as validity/terminator check (0 = end)
 * - Entry[1-7]: Additional data shorts (purpose varies by section and entry type)
 * - Total size: 16 bytes (8 shorts)
 *
 * This counting function is used by:
 * - mcb_data_processor.c: Determines processing loop bounds for each section
 * - Various MCB processing functions that need to know section size
 * - Game systems that iterate through MCB data entries
 *
 * Original function: FUN_0022a300
 * Address: 0x0022a300
 *
 * @param section_index MCB data section index (0-14)
 * @return Number of valid entries in the section (excluding zero terminator)
 */

#include "orphen_globals.h"

// Forward declarations for referenced functions
extern short *get_mcb_data_section_pointer(int section_index); // Get MCB data section pointer (FUN_0022a238)

int count_mcb_section_entries(int section_index)
{
  short *current_entry;
  int entry_count;
  short first_value;

  // Get pointer to the start of the MCB data section
  current_entry = get_mcb_data_section_pointer(section_index);

  if (current_entry == NULL)
  {
    return 0; // Invalid section or resource not found
  }

  // Initialize counter and read first entry's first value
  entry_count = 0;
  first_value = *current_entry;

  // Count entries until we find a zero terminator
  while (first_value != 0)
  {
    // Move to next entry (16 bytes = 8 shorts ahead)
    current_entry += 8;

    // Increment the count of valid entries
    entry_count++;

    // Read the first value of the next entry
    first_value = *current_entry;
  }

  return entry_count;
}

/*
 * ANALYSIS NOTES:
 *
 * MCB Data Structure Layout:
 * Each MCB section contains a sequence of 16-byte entries:
 *
 * Entry N:
 *   +0x00: short[0] - Entry validity flag (0 = terminator)
 *   +0x02: short[1] - Data field 1
 *   +0x04: short[2] - Data field 2
 *   +0x06: short[3] - Data field 3
 *   +0x08: short[4] - Data field 4
 *   +0x0A: short[5] - Data field 5
 *   +0x0C: short[6] - Data field 6
 *   +0x0E: short[7] - Data field 7
 *
 * Entry N+1:
 *   +0x10: Next entry begins...
 *
 * Terminator Entry:
 *   +0x00: 0x0000 (signals end of section)
 *
 * Usage Pattern:
 * This function is always called before get_mcb_data_section_pointer()
 * in processing loops to determine iteration bounds. The count returned
 * tells the calling code how many valid entries to process.
 *
 * MCB Data Purpose:
 * MCB1.BIN appears to be a structured game data archive containing:
 * - Map/scene configuration data
 * - Entity placement and properties
 * - Game object parameters
 * - Scene transition information
 * - Audio trigger data
 * - Various other game data types organized into 15 logical sections
 *
 * The entry counting ensures proper bounds checking when processing
 * any of the diverse game data stored in the MCB sections.
 */
