#include "orphen_globals.h"

// Forward declarations
extern uint *find_resource_by_id(uint resource_id); // Find resource data block (FUN_00267f90)

/**
 * Get text/resource pointer by index from resource table
 *
 * This function retrieves a text string or resource by its index from a loaded
 * resource table. It performs a two-stage lookup:
 * 1. Finds the resource table using a global resource ID (text_resource_table_id)
 * 2. Uses the resource table's offset array to locate the specific text/resource
 *
 * The resource table structure appears to be:
 * - Base pointer from find_resource_by_id()
 * - Offset +0x14: Pointer to offset array
 * - Offset array[index]: Contains offset to actual text/resource data
 * - Final result: Base pointer + offset from array
 *
 * This is commonly used for game text strings, menu items, and other indexed resources.
 * Text IDs like 0x3f-0x45 (menu items) and 0x26 (system text) use this system.
 *
 * Original function: FUN_0025b9e8
 * Address: 0x0025b9e8
 *
 * @param text_index Index into the text/resource table (0-based)
 * @return Pointer to the text/resource data, or invalid pointer if resource not found
 */
int get_text_resource(int text_index)
{
  int resource_base;

  // Find the text resource table using global resource ID
  resource_base = (int)find_resource_by_id(text_resource_table_id);

  // Calculate final text pointer:
  // 1. Get offset array pointer from resource base + 0x14
  // 2. Look up offset for this text index (text_index * 4 for 32-bit offsets)
  // 3. Add offset to resource base to get final text pointer
  return *(int *)(text_index * 4 + *(int *)(resource_base + 0x14)) + resource_base;
}

// Global variables for text resource system:

/**
 * Text resource table ID
 * Resource ID used to locate the main text/string table
 * Original: uGpffffade0
 */
extern uint text_resource_table_id;
