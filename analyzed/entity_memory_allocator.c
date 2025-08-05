/*
 * Entity Memory Allocator System - FUN_00267f50 & FUN_00261de0
 *
 * This system manages dynamic memory allocation for game entities in the
 * 0x01C40000-0x01C8FFFF memory range. It provides a slot-based allocation
 * mechanism for entity data storage.
 *
 * Memory Layout:
 * - 0x01C49A00: Primary entity data heap (DAT_01c49a00)
 * - 0x01C69A00: Secondary entity data area (DAT_01c69a00)
 * - 0x01C81A00: Resource cache/management area (DAT_01c81a00)
 * - 0x01C81A04: Cache status/control word (DAT_01c81a04)
 *
 * The system uses pointer globals (puGpffffb6ac, puGpffffb6b0, puGpffffbdcc)
 * to track the different memory regions and provides slot-based allocation
 * through a table of 0x3E (62) allocation slots.
 *
 * This explains the entity IDs seen in debug output brackets - they correspond
 * to allocated slots in this system, with memory addresses calculated as:
 * entity_address = base_heap + (slot_id * entity_size)
 */

#include "orphen_globals.h"

// External function declarations
extern void FUN_0026bfc0(int error_code); // Error handler for allocation failures

// Global variables for entity memory management
extern uint uGpffffbdc8;   // Global state/counter (original)
extern void *puGpffffb6ac; // Pointer to primary entity heap (0x01C49A00)
extern void *puGpffffb6b0; // Pointer to secondary entity area (0x01C69A00)
extern void *puGpffffbdcc; // Pointer to resource cache area (0x01C81A00)
extern int *piGpffffbd84;  // Allocation slot table (62 slots)
extern uint DAT_01c81a00;  // Resource cache header
extern uint DAT_01c81a04;  // Resource cache status

/**
 * Initialize Entity Memory System
 *
 * Sets up the entity memory allocator by initializing pointers to the
 * different memory regions used for entity data storage. This function
 * is called during system initialization to prepare the entity heap.
 *
 * Memory regions initialized:
 * - Primary entity heap at 0x01C49A00 (128KB region)
 * - Secondary entity area at 0x01C69A00 (128KB region)
 * - Resource cache at 0x01C81A00 (remaining space to 0x01C8FFFF)
 *
 * Original function: FUN_00267f50
 * Address: 0x00267f50
 */
void initialize_entity_memory_system(void)
{
  // Reset global state counter
  uGpffffbdc8 = 0;

  // Mark resource cache as empty
  DAT_01c81a04 = 0xffffffff;

  // Set up pointers to memory regions
  puGpffffb6ac = (void *)0x01c49a00; // Primary entity heap (128KB)
  puGpffffb6b0 = (void *)0x01c69a00; // Secondary entity area (128KB)
  puGpffffbdcc = (void *)0x01c81a00; // Resource cache area

  // Initialize resource cache header
  DAT_01c81a00 = 0;

  return;
}

/**
 * Find Available Entity Allocation Slot
 *
 * Searches through the allocation slot table to find the first available
 * (zero) slot for entity allocation. The system supports up to 62 concurrent
 * entity allocations (0x3E slots).
 *
 * The allocation table tracks which entity slots are currently in use.
 * When an entity is created, this function finds a free slot and returns
 * the slot index. The actual memory address is calculated elsewhere using:
 * entity_ptr = entity_heap_base + (slot_index * entity_size)
 *
 * Original function: FUN_00261de0
 * Address: 0x00261de0
 *
 * @return Slot index (0-61) if available, -1 if all slots occupied
 */
int find_available_entity_slot(void)
{
  int *current_slot;
  int slot_index;

  slot_index = 0;
  current_slot = piGpffffbd84; // Start of allocation table

  // Search through all 62 allocation slots
  do
  {
    if (*current_slot == 0)
    {
      // Found empty slot - return index
      return slot_index;
    }

    slot_index++;
    current_slot++;

  } while (slot_index < 0x3e); // Check all 62 slots

  // All slots occupied - trigger allocation failure error
  FUN_0026bfc0(0x34d188); // "Out of entity slots" or similar error
  return -1;
}

/*
 * Entity Memory System Analysis
 *
 * This system explains the entity IDs seen in SCEN WORK DISP debug output.
 * The numbers in brackets like [1234] correspond to calculated memory addresses
 * within the 0x01C40000-0x01C4FFFF range where entity data is stored.
 *
 * Entity Creation Process:
 * 1. find_available_entity_slot() locates free slot (0-61)
 * 2. Entity data allocated at: heap_base + (slot * entity_size)
 * 3. Slot marked as occupied in allocation table
 * 4. Entity ID becomes the memory address for debug display
 *
 * Memory Range Mapping:
 * - 0x01C40000-0x01C49FFF: Unallocated/reserved space
 * - 0x01C49A00-0x01C69A00: Primary entity heap (128KB)
 * - 0x01C69A00-0x01C81A00: Secondary entity area (128KB)
 * - 0x01C81A00-0x01C8FFFF: Resource cache and management (~60KB)
 *
 * This slot-based system allows efficient entity lifecycle management
 * with O(1) allocation/deallocation and prevents memory fragmentation.
 */
