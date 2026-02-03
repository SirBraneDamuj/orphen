// Opcode 0x68 — check_entity_tracking_state
// Original: FUN_0025fae0
//
// Summary:
// - Reads 3 script parameters via bytecode evaluator
// - Selects target entity using first parameter
// - Calls FUN_00257f18 to check entity tracking state
// - FUN_00257f18 queries two entity tracking channels and returns true if either is active
//
// Script Parameters (3 total):
// - param[0]: Entity selector (index or 0x100 for current)
// - param[1]: Unused (read but not used)
// - param[2]: Unused (read but not used)
//
// Processing:
// - Evaluates 3 parameters using bytecode_interpreter
// - Stack layout: params at offsets +0x30, +0x34, +0x38
// - Selects target entity via select_current_object_frame (using param[0])
// - Calls FUN_00257f18(entity) which returns bool indicating tracking state
//
// FUN_00257f18 Behavior (from source analysis):
// - Calls FUN_0020dd78(entity, 2) to get tracking channel 2 handle
// - Calls FUN_0020d968(entity, channel2) to query channel 2 state
// - Calls FUN_0020dd78(entity, 1) to get tracking channel 1 handle
// - Calls FUN_0020d968(entity, channel1) to query channel 1 state
// - Returns true if (channel1_state + channel2_state) != 0
// - This checks if entity has any active tracking/targeting operations
//
// Related Functions:
// - FUN_0020dd78: Get tracking channel handle (channel 1 or 2)
// - FUN_0020d968: Query tracking channel state (returns int, 0=inactive)
// - Opcode 0x63: set_entity_tracking_and_position - sets tracking mode
// - Opcode 0x67: set_entity_target_position_with_rotation - updates tracking angle
//
// Entity Tracking System:
// - Entities have 2 tracking channels (1 and 2)
// - Each channel can track another entity or target position
// - FUN_0020dd78 retrieves channel handle from entity
// - FUN_0020d968 checks if channel is active (non-zero = active)
// - This opcode queries both channels and returns if either is tracking
//
// Global Side Effects:
// - Updates DAT_00355044 (current entity pointer) via select_current_object_frame
// - No other side effects (read-only query operation)
//
// Return Value:
// - None (void) - opcode doesn't submit result to VM
// - However, FUN_00257f18 returns bool that could be used in future variants
//
// PS2-specific notes:
// - Tracking system appears to use handles/IDs rather than direct pointers
// - Channel 1 and 2 likely correspond to different tracking modes (camera, target, etc.)
// - The sum check (ch1 + ch2 != 0) suggests states are integers, not just booleans
//
// Relationship to other opcodes:
// - Opcode 0x63: Sets tracking mode at entity+0xA0, links tracker→target
// - Opcode 0x67: Updates target position/rotation for tracking
// - Opcode 0x68: Queries if entity has active tracking (this opcode)
// - Opcode 0x69: Clears entity tracking state (likely pairs with this)
//
// Returns: void
//
// Original signature: void FUN_0025fae0(void)

#include <stdint.h>
#include <stdbool.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Select current entity frame by index or direct pointer
extern void select_current_object_frame(uint32_t selector, void *fallbackPtr);

// Current selected entity pointer
extern uint32_t uGpffffb0d4;

// Check entity tracking state (returns true if any channel is active)
// Queries both tracking channels (1 and 2) and returns logical OR of their states
extern bool FUN_00257f18(void *entity);

// Analyzed implementation
void opcode_0x68_check_entity_tracking_state(void)
{
  uint32_t saved_entity;
  uint32_t params[3]; // Stack offsets: +0x30, +0x34, +0x38

  // Save current entity pointer
  saved_entity = uGpffffb0d4;

  // Read 3 script parameters
  bytecode_interpreter(&params[0]); // Param 0: entity selector
  bytecode_interpreter(&params[1]); // Param 1: unused
  bytecode_interpreter(&params[2]); // Param 2: unused

  // Select target entity
  select_current_object_frame(params[0], saved_entity);

  // Query entity tracking state (result not used by this opcode variant)
  // Returns true if entity has active tracking on channel 1 or 2
  FUN_00257f18(uGpffffb0d4);

  return;
}

// Original signature wrapper
void FUN_0025fae0(void)
{
  opcode_0x68_check_entity_tracking_state();
}
