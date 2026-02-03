// Opcode 0x69 — clear_entity_tracking_state
// Original: FUN_0025fb30
//
// Summary:
// - Reads 3 script parameters via bytecode evaluator
// - Selects target entity using first parameter
// - Calls FUN_00257f78 to clear entity tracking state
// - FUN_00257f78 clears both tracking channels (1 and 2) for the entity
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
// - Calls FUN_00257f78(entity) to clear tracking channels
// - Returns 0 (always)
//
// FUN_00257f78 Behavior (from source analysis):
// - Calls FUN_0020dd78(entity, 2) to get tracking channel 2 handle
// - Calls FUN_0020d9c8(entity, channel2) to clear/reset channel 2
// - Calls FUN_0020dd78(entity, 1) to get tracking channel 1 handle
// - Calls FUN_0020d9c8(entity, channel1) to clear/reset channel 1
// - Returns void (no return value)
// - This clears all tracking/targeting operations for the entity
//
// Related Functions:
// - FUN_0020dd78: Get tracking channel handle (channel 1 or 2)
// - FUN_0020d9c8: Clear/reset tracking channel state
// - Opcode 0x63: set_entity_tracking_and_position - sets up tracking
// - Opcode 0x67: set_entity_target_position_with_rotation - updates tracking
// - Opcode 0x68: check_entity_tracking_state - queries tracking state
//
// Entity Tracking System:
// - Entities have 2 tracking channels (1 and 2)
// - Each channel can track another entity or target position
// - FUN_0020dd78 retrieves channel handle from entity
// - FUN_0020d9c8 clears/resets the channel to inactive state
// - This opcode clears both channels, disabling all tracking
//
// Global Side Effects:
// - Updates DAT_00355044 (current entity pointer) via select_current_object_frame
// - Clears entity's tracking channel 1 state (via FUN_0020d9c8)
// - Clears entity's tracking channel 2 state (via FUN_0020d9c8)
// - Entity will no longer track targets or update rotation automatically
//
// Return Value:
// - Returns 0 (always) - standard success return for void-like opcodes
//
// PS2-specific notes:
// - Tracking system appears to use handles/IDs rather than direct pointers
// - Channel 1 and 2 likely correspond to different tracking modes (camera, target, etc.)
// - Clearing tracking doesn't affect entity position, only future automatic updates
//
// Relationship to other opcodes:
// - Opcode 0x63: Sets tracking mode at entity+0xA0, links tracker→target
// - Opcode 0x67: Updates target position/rotation for tracking
// - Opcode 0x68: Queries if entity has active tracking
// - Opcode 0x69: Clears entity tracking state (this opcode) - cleanup operation
//
// Typical Usage Pattern:
// 1. Opcode 0x63: Set up entity tracking (link entities, set mode)
// 2. Opcode 0x67: Update target position multiple times (smooth tracking)
// 3. Opcode 0x68: Query if tracking is still active (conditional logic)
// 4. Opcode 0x69: Clear tracking when cutscene/sequence ends
//
// Returns: 0 (always)
//
// Original signature: undefined8 FUN_0025fb30(void)

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Select current entity frame by index or direct pointer
extern void select_current_object_frame(uint32_t selector, void *fallbackPtr);

// Current selected entity pointer
extern uint32_t uGpffffb0d4;

// Clear entity tracking state (clears both channels 1 and 2)
// Disables automatic tracking/rotation updates for the entity
extern void FUN_00257f78(void *entity);

// Analyzed implementation
uint64_t opcode_0x69_clear_entity_tracking_state(void)
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

  // Clear entity tracking state (both channels 1 and 2)
  // Entity will stop tracking targets and halt automatic rotation updates
  FUN_00257f78(uGpffffb0d4);

  return 0; // Success
}

// Original signature wrapper
uint64_t FUN_0025fb30(void)
{
  return opcode_0x69_clear_entity_tracking_state();
}
