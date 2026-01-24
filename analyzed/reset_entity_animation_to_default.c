// Entity Animation Reset to Default (analyzed)
// Original: FUN_00252d88
// Address: 0x00252d88
//
// Summary:
// - Resets an entity's animation to the default idle/neutral state (state 0, param 1).
// - Clears animation-related control fields and ensures character counter is valid.
// - Called when exiting battle mode, completing actions, or returning to normal state.
//
// Inputs:
// - entity (uint64): Pointer to entity structure (typically in DAT_0058beb0 pool)
//
// Entity Structure Offsets:
// - +0x1B6: uint16 secondary_animation_state - cleared to 0
// - +0x1B8: uint16 animation_loop_counter - set to 1 if currently 0
//
// Side Effects:
// - Calls set_entity_animation_state(entity, 0, 1) - state 0 = idle, param 1 = default
// - Clears secondary animation state at +0x1B6
// - Ensures animation loop counter is at least 1
// - Clears global character counter cGpffffb6e1 if it's negative
//
// Usage Examples (from codebase):
// - FUN_00252d88(&DAT_0058beb0): Reset main player to idle animation
// - Called after exiting battle mode (opcode 0x6D control flow)
// - Called when AI mode changes from battle→field
//
// Notes:
// - Animation state 0 is the default "neutral" state (standing idle, field mode)
// - The function ensures loop counter >= 1, suggesting animations need valid play count
// - cGpffffb6e1 appears to be a global character selection/active index
// - Offset 0x1B6 may be a secondary/queued animation state

#include <stdint.h>

// External animation state setter
extern void set_entity_animation_state(int entity_ptr, uint16_t animation_state, uint16_t animation_param);

// Global character counter/index (reset if negative)
extern char cGpffffb6e1;

// Original signature: void FUN_00252d88(undefined8 param_1)
void reset_entity_animation_to_default(uint64_t entity)
{
  int entity_ptr = (int)entity;
  
  // Set animation to default state 0, parameter 1 (idle/neutral)
  set_entity_animation_state(entity, 0, 1);
  
  // Clear secondary animation state
  *(uint16_t *)(entity_ptr + 0x1B6) = 0;
  
  // Ensure animation loop counter is at least 1
  if (*(int16_t *)(entity_ptr + 0x1B8) == 0)
  {
    *(uint16_t *)(entity_ptr + 0x1B8) = 1;
  }
  
  // Reset global character counter if invalid (negative)
  if (cGpffffb6e1 < 0)
  {
    cGpffffb6e1 = 0;
  }
}

// Original signature wrapper for compatibility
void FUN_00252d88(uint64_t param_1)
{
  reset_entity_animation_to_default(param_1);
}
