/*
 * Initialize Entity Memory System - FUN_00267f50
 *
 * Sets up the entity memory allocator by initializing pointers to different
 * memory regions used for entity data storage. This function is called during
 * system initialization to prepare the entity heap management system.
 *
 * Memory regions initialized:
 * - Primary entity heap at 0x01C49A00 (128KB region)
 * - Secondary entity area at 0x01C69A00 (128KB region)
 * - Resource cache at 0x01C81A00 (remaining space)
 *
 * This system manages the 0x01C40000-0x01C8FFFF memory range where entity
 * data is dynamically allocated. The entity IDs seen in debug output
 * correspond to memory addresses within this allocated heap space.
 *
 * Original function: FUN_00267f50
 * Address: 0x00267f50
 */

#include "orphen_globals.h"

// Global variables for entity memory management (not yet in orphen_globals.h)
extern uint uGpffffbdc8;   // Global state/counter - reset to 0 on init
extern void *puGpffffb6ac; // Pointer to primary entity heap (0x01C49A00)
extern void *puGpffffb6b0; // Pointer to secondary entity area (0x01C69A00)
extern void *puGpffffbdcc; // Pointer to resource cache area (0x01C81A00)
extern uint DAT_01c49a00;  // Primary entity heap data start
extern uint DAT_01c69a00;  // Secondary entity area data start
extern uint DAT_01c81a00;  // Resource cache header/data start
extern uint DAT_01c81a04;  // Resource cache status word

/**
 * Initialize entity memory management system
 *
 * Prepares the memory allocator for entity data by setting up pointers
 * to the different memory regions and clearing status flags. This creates
 * the foundation for the slot-based entity allocation system.
 */
void FUN_00267f50(void)
{
  // Reset global state counter
  uGpffffbdc8 = 0;

  // Mark resource cache as empty/uninitialized
  DAT_01c81a04 = 0xffffffff;

  // Set up pointers to memory regions in 0x01C4-0x01C8 range
  puGpffffb6ac = &DAT_01c49a00; // Primary entity heap (128KB)
  puGpffffb6b0 = &DAT_01c69a00; // Secondary entity area (128KB)
  puGpffffbdcc = &DAT_01c81a00; // Resource cache area

  // Initialize resource cache header to empty
  DAT_01c81a00 = 0;

  return;
}
