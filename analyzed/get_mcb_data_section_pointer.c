/*
 * MCB Data Section Pointer Function - FUN_0022a238
 *
 * Returns a pointer to the data for a specific MCB data section. This function
 * accesses the MCB resource data structure to locate the appropriate data section
 * based on the section index parameter.
 *
 * The function works by:
 * 1. Finding the MCB resource data block using find_resource_by_id()
 * 2. Accessing an offset table at +0x1c in the resource header
 * 3. Using conditional logic based on DAT_003555d3 flag to select data
 * 4. Returning a pointer to the actual data section
 *
 * Data Structure Analysis:
 * - MCB resource header contains offset table at +0x1c
 * - Each section has a 4-byte offset entry (param_1 * 4)
 * - DAT_003555d3 appears to be a mode flag (MCB0 vs MCB1?)
 * - When flag is 0: Uses section-specific offsets (param_1 * 4 indexing)
 * - When flag is 1: Uses fixed offset +0x38 (possibly MCB1 data)
 *
 * This function is critical for MCB data access and is used by:
 * - mcb_data_processor.c: Gets data pointers for sections 0-14
 * - count_mcb_section_entries: Entry counting function (FUN_0022a300)
 * - Various other MCB processing functions
 *
 * Original function: FUN_0022a238
 * Address: 0x0022a238
 *
 * @param section_index MCB data section index (0-14)
 * @return Pointer to MCB data section, or NULL if resource not found
 */

#include "orphen_globals.h"

// Forward declarations for referenced functions
extern uint *find_resource_by_id(uint resource_id); // Find resource by ID (FUN_00267f90)

// Global variables used by this function
extern uint DAT_00354d50; // MCB resource ID
extern char DAT_003555d3; // MCB data source flag (0=MCB0, 1=MCB1?)

short *get_mcb_data_section_pointer(int section_index)
{
  uint *mcb_resource;
  int section_offset;

  // Find the MCB resource data block by ID
  mcb_resource = find_resource_by_id(DAT_00354d50);

  if (mcb_resource == NULL)
  {
    return NULL; // Resource not found
  }

  // Access the MCB data based on mode flag
  if (DAT_003555d3 == 0)
  {
    // Mode 0: Use section-specific offset table
    // MCB resource header has offset table at +0x1c
    // Each section has 4-byte offset entry indexed by section_index
    uint *offset_table = (uint *)(mcb_resource + 0x1c / 4);
    section_offset = offset_table[section_index];
  }
  else
  {
    // Mode 1: Use fixed offset (MCB1 data?)
    // Access data at fixed offset +0x38 from offset table
    uint *offset_table = (uint *)(mcb_resource + 0x1c / 4);
    section_offset = *(uint *)((char *)offset_table + 0x38);
  }

  // Return pointer to actual data section
  // Add section_offset to mcb_resource base to get final pointer
  return (short *)((char *)mcb_resource + section_offset);
}

/*
 * ANALYSIS NOTES:
 *
 * Resource Structure Layout (hypothesized):
 * MCB Resource Header:
 *   +0x00: Standard resource header (8 bytes)
 *   +0x08: MCB-specific data begins
 *   +0x1c: Offset table pointer/base (used for section indexing)
 *   +0x38: Alternate data offset (used when DAT_003555d3 = 1)
 *
 * Section Access Patterns:
 * - Normal mode (DAT_003555d3 = 0): section_index * 4 byte indexing
 * - Alternate mode (DAT_003555d3 = 1): Fixed +0x38 offset
 *
 * This suggests the MCB system can access two different data sets:
 * - MCB0.BIN data (indexed by section)
 * - MCB1.BIN data (fixed offset access)
 *
 * The function is used extensively throughout the MCB processing system
 * and appears to be the primary interface for accessing compressed text
 * data that you've identified in MCB1.BIN.
 */
