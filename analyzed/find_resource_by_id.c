#include "orphen_globals.h"

/**
 * Find resource data block by ID in cached resource table
 *
 * This function searches through a resource cache/table to find a data block
 * matching the specified resource ID. It implements a linked-list traversal
 * through resource entries, each with a size/offset field and an ID field.
 *
 * The search process:
 * 1. Checks if cache is initialized (first entry ID != 0xffffffff)
 * 2. Compares target ID against current entry ID (masked with 0x7fffffff)
 * 3. If no match, advances to next entry using size/offset from current entry
 * 4. Continues until match found or end marker (0xffffffff) reached
 *
 * Resource entry structure appears to be:
 * - [0]: Size/offset to next entry (bottom 2 bits masked out)
 * - [4]: Resource ID (or 0xffffffff for end marker)
 * - [8+]: Resource data begins here
 *
 * Original function: FUN_00267f90
 * Address: 0x00267f90
 *
 * @param resource_id Resource identifier to search for (masked with 0x7fffffff)
 * @return Pointer to resource data (offset +8 from entry), or NULL if not found
 */
uint *find_resource_by_id(uint resource_id)
{
  uint entry_size;
  uint *current_entry;

  // Check if resource cache is initialized
  if (resource_cache_start[1] != 0xffffffff)
  {
    current_entry = resource_cache_start;

    // Check first entry
    if (resource_cache_start[1] == (resource_id & 0x7fffffff))
    {
    FOUND_RESOURCE:
      if (current_entry[1] != 0xffffffff)
      {
        return current_entry + 2; // Return pointer to resource data (+8 bytes)
      }
    }
    else
    {
      // Search through linked entries
      entry_size = *resource_cache_start;
      while (true)
      {
        // Advance to next entry using size (mask off bottom 2 bits)
        current_entry = (uint *)((int)current_entry + (entry_size & 0xfffffffc) + 8);

        // Check for end of list
        if (current_entry[1] == 0xffffffff)
          break;

        // Check if this entry matches target ID
        if (current_entry[1] == (resource_id & 0x7fffffff))
          goto FOUND_RESOURCE;

        entry_size = *current_entry;
      }
    }
  }
  return (uint *)0x0; // Resource not found
}

// Global variables for resource system:

/**
 * Resource cache start pointer
 * Points to the beginning of the resource cache/table
 * Original: puGpffffbdcc
 */
extern uint *resource_cache_start;
