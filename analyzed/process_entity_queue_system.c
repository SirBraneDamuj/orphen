// Original function: FUN_0025ce30
// Processes a queue/array of entities with audio and graphics handling
// Iterates through up to 4 entities, checking flags and managing state transitions

#include "orphen_globals.h"

// External globals (DAT_* variables not yet analyzed)
extern uint32_t DAT_00571e44;
extern uint32_t DAT_00571e40;
extern uint32_t DAT_00571e48;
extern int32_t iGpffffb64c;
extern uint32_t uGpffffbd70;
extern uint32_t uGpffffbd74;
extern int32_t iGpffffbd84;
extern int32_t iGpffffb0e8;
extern uint16_t uGpffffb0f4;

// External functions (FUN_* functions not yet analyzed)
extern int FUN_00261de0(void);                 // Find available entity allocation slot - analyzed
extern void FUN_00237b38(long param_1);        // Audio/graphics handler function
extern long FUN_00266368(uint16_t flag_value); // Get flag state - already analyzed

void process_entity_queue_system(void)
{
  uint16_t entity_flags;
  uint16_t *entity_ptr;
  uint32_t timer_value;
  int allocation_index;
  long flag_check_result;
  int *entity_data_ptr;
  int struct_offset;
  int entity_index;
  uint32_t *timer_ptr;

  // Start with first entity timer at DAT_00571e44
  timer_ptr = (uint32_t *)&DAT_00571e44;
  entity_index = 0;
  struct_offset = 0;

  do
  {
    // Get entity pointer from previous slot (puVar9[-1])
    // This suggests the data layout: [entity_ptr][unknown][timer_value]
    entity_ptr = (uint16_t *)timer_ptr[-1];

    if (entity_ptr == (uint16_t *)0x0)
      goto advance_to_next_entity;

    entity_flags = entity_ptr[1]; // Flags at offset 2 in entity structure

    if (entity_flags == 0)
    {
    entity_timer_update:
      timer_value = *timer_ptr;

    entity_state_check:
      // Check if timer (shifted right by 5) is less than entity's first value
      if ((timer_value >> 5 & 0xffff) < (uint32_t)*entity_ptr)
      {
        // Increment timer by some global value
        *timer_ptr = timer_value + iGpffffb64c;
      }
      else
      {
        // Timer expired - process entity completion
        timer_value = *(uint32_t *)(entity_ptr + 2); // Get value at offset 4

        // Check if value is within valid range for direct processing
        if ((timer_value < uGpffffbd70) || (uGpffffbd74 <= timer_value))
        {
          // Value outside range - use allocation system
          allocation_index = FUN_00261de0(); // Get allocation slot
          *(int *)(allocation_index * 4 + iGpffffbd84) = *(int *)(entity_ptr + 2) + iGpffffb0e8;
        }
        else
        {
          // Value in range - direct function call
          FUN_00237b38(timer_value + iGpffffb0e8);
        }

        // Update entity data structures at DAT_00571e40 + offset
        entity_data_ptr = (int *)((int)&DAT_00571e40 + struct_offset);
        *entity_data_ptr = *entity_data_ptr + 8; // Advance pointer by 8 bytes

        // Increment counter at DAT_00571e48 + offset
        *(int *)(&DAT_00571e48 + struct_offset) = *(int *)(&DAT_00571e48 + struct_offset) + 1;

        // Check if we've reached end of data (next entry is 0)
        if (*(int *)(*entity_data_ptr + 4) == 0)
        {
          *entity_data_ptr = 0; // Reset pointer
        }

        // Clear timer value
        *(uint32_t *)(&DAT_00571e44 + struct_offset) = 0;
      }
    }
    else if ((entity_flags & 0x8000) == 0)
    {
      // Flag without high bit set - check some condition
      flag_check_result = FUN_00266368(entity_ptr[1]); // Check flag value
      if (flag_check_result != 0)
        goto entity_timer_update;
    }
    else if ((entity_flags & (uGpffffb0f4 | 0x8000)) == entity_flags)
    {
      // Special flag combination - go directly to state check
      timer_value = *timer_ptr;
      goto entity_state_check;
    }

  advance_to_next_entity:
    entity_index = entity_index + 1;
    struct_offset = struct_offset + 0xc; // 12-byte stride between entities
    timer_ptr = timer_ptr + 3;           // 3 uint entries per entity

    if (3 < entity_index)
    { // Process maximum of 4 entities (0-3)
      return;
    }
  } while (true);
}
