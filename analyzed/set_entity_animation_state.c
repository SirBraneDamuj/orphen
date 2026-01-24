// Entity Animation State Setter (analyzed)
// Original: FUN_00225bf0
// Address: 0x00225bf0
//
// Summary:
// - Sets the animation state for an entity in the 0xEC stride entity pool (DAT_0058beb0).
// - Updates multiple animation-related fields: state ID, substate ID, timer, and control flags.
// - This is the primary function for triggering character animations (walk, attack, death, etc.).
//
// Inputs:
// - entity_ptr (int): Pointer to entity structure in pool (base + index * 0xEC)
// - animation_state (uint16): Primary animation state ID (0-255+)
// - animation_param (uint16): Animation parameter/substate ID
//
// Entity Structure Offsets (animation-related):
// - +0x06: uint16 flags - bits cleared: 0x00C7 (keeps bits 0x0038)
// - +0x60: uint16 animation_state - primary state ID (param_2)
// - +0xA0: uint16 animation_param - substate/parameter (param_3)
// - +0xA2: uint16 animation_next_state - set to 0xFFFF (no queued transition)
// - +0xA4: uint16 animation_timer - set to 999 (countdown/duration)
// - +0xA8: uint16 animation_frame - set to 0 (reset frame counter)
//
// Side Effects:
// - Clears animation control flags in entity[+0x06] (keeps only bits 0x0038, clears 0x00C7)
// - Resets animation frame counter to 0
// - Sets animation timer to 999
// - Marks next state as undefined (0xFFFF)
//
// Usage Examples (from codebase):
// - FUN_00225bf0(0x58beb0, 10, 1): Set player to animation state 10, param 1
// - FUN_00225bf0(player_entity, 0x19, 0xd): Death animation (state 0x19, param 0xd)
// - FUN_00225bf0(player_entity, 0x18, 0x20): Alternative death state
// - FUN_00225bf0(entity, 1, 0): Initialize entity animation (state 1, param 0)
// - FUN_00225bf0(entity, 8, 3): Re-init animation (state 8, param 3)
//
// Notes:
// - Animation state 0 appears to be "idle" or "default"
// - State 0x19 (25) and 0x18 (24) are death-related
// - States 0x16-0x17 appear to be damage/hurt reactions
// - The timer value 999 is fixed; actual animation duration likely determined by data tables
// - Clearing flags 0x00C7 may disable animation interrupts, state transitions, or loop flags

#include <stdint.h>

// Original signature: void FUN_00225bf0(int param_1, undefined2 param_2, undefined2 param_3)
void set_entity_animation_state(int entity_ptr, uint16_t animation_state, uint16_t animation_param)
{
  // Set primary animation state ID
  *(uint16_t *)(entity_ptr + 0x60) = animation_state;
  
  // Set animation timer (fixed 999 countdown)
  *(uint16_t *)(entity_ptr + 0xA4) = 999;
  
  // Set animation parameter/substate
  *(uint16_t *)(entity_ptr + 0xA0) = animation_param;
  
  // Mark next state as undefined (no queued transition)
  *(uint16_t *)(entity_ptr + 0xA2) = 0xFFFF;
  
  // Clear animation control flags (keep only bits 0x0038, clear 0x00C7)
  // Bitwise: flags = (flags & 0xFF38) - clears bits 0-2, 6-7 in lower byte
  *(uint16_t *)(entity_ptr + 6) = *(uint16_t *)(entity_ptr + 6) & 0xFF38;
  
  // Reset animation frame counter
  *(uint16_t *)(entity_ptr + 0xA8) = 0;
}

// Original signature wrapper for compatibility
void FUN_00225bf0(int param_1, uint16_t param_2, uint16_t param_3)
{
  set_entity_animation_state(param_1, param_2, param_3);
}
