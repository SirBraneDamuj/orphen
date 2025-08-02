/*
 * Entity Update Processing Loop
 * Original function: FUN_002261e0
 * Address: 0x002261e0
 *
 * This function processes all active entities in the game world. It iterates through
 * an array of 256 entities, checking each one for activation and processing updates
 * as needed. Uses PS2 scratchpad memory for temporary stack allocation.
 *
 * Key behaviors:
 * - Allocates 0x170 (368) bytes on PS2 scratchpad stack for temporary data
 * - Loops through 256 entities starting at 0x58beb0, each 0xec (236) bytes in size
 * - Checks entity activation flags and status before processing
 * - Calls entity-specific update functions for active entities
 * - Restores scratchpad stack pointer when finished
 *
 * Entity processing conditions:
 * 1. Entity must be marked as active in the activation array (DAT_005a96b0)
 * 2. Entity status flags must not have the 0x800 bit set (likely "disabled" flag)
 * 3. Entity update function (FUN_00225c90) must set a negative status flag
 * 4. If all conditions met, calls the main entity processor (FUN_002262c0)
 */

#include "orphen_globals.h"

// External function declarations
extern void FUN_0026bf90(int param);                          // Stack overflow handler
extern void FUN_00225c90(void *entity_ptr);                   // Entity update/validation function
extern void FUN_002262c0(void *entity_ptr, void *stack_data); // Main entity processor

// Global variables
extern long *gpu_command_buffer_start;    // DAT_70000000 - PS2 scratchpad stack pointer
extern void *entity_array_base;           // DAT_0058beb0 - Base of entity array (256 entities)
extern char entity_activation_flags[256]; // DAT_005a96b0 - Entity activation status array
extern int current_frame_data;            // DAT_003556dc - Current frame/timing data

/*
 * Process Entity Update Loop
 *
 * Iterates through all 256 game entities and processes active ones.
 * Uses PS2 scratchpad memory for efficient temporary storage during processing.
 */
void process_entity_update_loop(void)
{
  void *stack_frame;
  void *current_entity;
  int entity_index;

  // Save current scratchpad stack pointer and allocate 368 bytes for temporary data
  stack_frame = (void *)gpu_command_buffer_start;
  gpu_command_buffer_start = (long *)((char *)gpu_command_buffer_start + 0x170);

  // Check for scratchpad memory overflow (PS2 scratchpad is 0x70000000-0x70003fff)
  if ((unsigned long)gpu_command_buffer_start > 0x70003fff)
  {
    FUN_0026bf90(0); // Handle stack overflow
  }

  // Initialize entity pointer to start of entity array
  current_entity = entity_array_base;

  // Store frame data in allocated stack space (offset 0x44 = 68 bytes in)
  *(short *)((char *)stack_frame + 0x44) = (short)current_frame_data;

  entity_index = 0;

  // Process all 256 entities
  do
  {
    // Check if entity is active and not disabled
    if ((entity_activation_flags[entity_index] > 0) &&   // Entity is marked active
        ((((short *)current_entity)[1] & 0x800) == 0) && // Entity not disabled (0x800 flag clear)
        (FUN_00225c90(current_entity),                   // Call entity update function
         ((short *)current_entity)[0xc9] < 0))
    { // Check if update set negative status flag

      // Process the active entity
      FUN_002262c0(current_entity, stack_frame);
    }

    entity_index++;
    current_entity = (char *)current_entity + 0xec; // Move to next entity (236 bytes each)

  } while (entity_index < 0x100); // Process all 256 entities

  // Restore scratchpad stack pointer
  gpu_command_buffer_start = (long *)stack_frame;

  return;
}
