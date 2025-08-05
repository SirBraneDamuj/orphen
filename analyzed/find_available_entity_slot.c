/*
 * Find Available Entity Allocation Slot - FUN_00261de0
 *
 * Searches through the allocation slot table to find the first available
 * (zero) slot for entity allocation. The system supports up to 62 concurrent
 * entity allocations (0x3E slots total).
 *
 * The allocation table tracks which entity slots are currently in use.
 * When an entity needs to be created, this function finds a free slot
 * and returns the slot index. The actual memory address is calculated
 * elsewhere using: entity_ptr = entity_heap_base + (slot_index * entity_size)
 *
 * This explains how entity IDs in debug output (like [1234]) are generated -
 * they represent actual memory addresses in the 0x01C40000-0x01C4FFFF range
 * where entity data is allocated.
 *
 * Original function: FUN_00261de0
 * Address: 0x00261de0
 */

#include "orphen_globals.h"

// Forward declaration for error handling
extern void FUN_0026bfc0(uint error_code); // Error handler - triggers when allocation fails

// Global variables for slot-based allocation (not yet in orphen_globals.h)
extern int *piGpffffbd84; // Allocation slot table (62 entries, 4 bytes each)

/**
 * Find first available entity allocation slot
 *
 * Scans the allocation table to locate an empty slot for new entity creation.
 * Returns slot index on success, -1 on failure with error triggered.
 *
 * @return Slot index (0-61) if available, -1 if all slots occupied
 */
int FUN_00261de0(void)
{
  int *current_slot_ptr;
  int slot_index;

  // Initialize search from first slot
  slot_index = 0;
  current_slot_ptr = piGpffffbd84; // Start of allocation table

  // Search through all available slots
  do
  {
    // Check if current slot is empty (value == 0)
    if (*current_slot_ptr == 0)
    {
      // Found available slot - return its index
      return slot_index;
    }

    // Move to next slot
    slot_index = slot_index + 1;
    current_slot_ptr = current_slot_ptr + 1;

  } while (slot_index < 0x3e); // Check all 62 slots (0-61)

  // All slots occupied - trigger allocation failure error
  FUN_0026bfc0(0x34d188); // Error code - likely "Out of entity slots"
  return -1;              // Allocation failed
}
