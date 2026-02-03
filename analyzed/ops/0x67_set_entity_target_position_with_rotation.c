// Opcode 0x67 — set_entity_target_position_with_rotation
// Original: FUN_0025fa40
//
// Summary:
// - Reads 6 script parameters via bytecode evaluator
// - Normalizes 2 position coordinates by global divisor fGpffff8c70
// - Calls FUN_00257c78 with normalized float positions and raw param values
// - FUN_00257c78 appears to set entity target position/rotation for tracking/camera system
//
// Script Parameters (6 total):
// - params[0-3]: Raw integer values (likely entity selectors or mode flags)
// - params[4-5]: Position coordinates (normalized to float by fGpffff8c70)
//
// Processing:
// - Evaluates all 6 parameters using bytecode_interpreter
// - Stack layout: params stored at stack offsets +0x40, +0x44, +0x48, +0x4C, +0x30, +0x2C
// - Position values (params 4 & 5) converted to float: (int)value / fGpffff8c70
// - Selects target entity via select_current_object_frame (using params[0])
// - Calls FUN_00257c78(normalized_x, normalized_z, entity, param3)
//
// FUN_00257c78 Behavior (from source analysis):
// - Calculates angle from entity position to target: atan2(param2 - entity.Z, param1 - entity.X)
// - Compares current rotation (entity+0x5C) against target angle
// - Uses tracking mode field at entity+0xA0 (same as opcode 0x63)
// - Performs angle difference calculations and distance thresholds
// - Complex logic involving fGpffff8a68/6c/70/74 threshold constants
// - Allocates VIF/DMA buffer space (DAT_70000000 management)
// - Calls FUN_0020da68 (appears to be entity tracking/rotation update)
//
// Entity Offsets Referenced (by FUN_00257c78):
// - +0x20: float X position
// - +0x24: float Z position (horizontal plane)
// - +0x5C: Current rotation angle
// - +0xA0: Tracking mode (same field used by opcode 0x63)
//
// Global Side Effects:
// - Updates DAT_00355044 (current entity pointer) via select_current_object_frame
// - Allocates 0x40 bytes in DAT_70000000 buffer (VIF/DMA packet management)
// - Calls FUN_0020da68 twice with different parameters (tracking system updates)
//
// PS2-specific notes:
// - DAT_70000000 appears to be VU0/VIF buffer pointer for DMA packet building
// - Threshold check 0x70003FFF prevents buffer overflow
// - FUN_0026bf90(0) called on buffer overflow (likely reset/error handler)
// - The angle calculations use atan2 (FUN_00305408) for 2D horizontal rotation
// - Distance threshold 0.5 (in normalized units) determines near/far behavior
//
// Relationship to other opcodes:
// - Opcode 0x63: Also uses entity+0xA0 tracking mode, links tracker→target
// - Opcode 0x67: Sets target position/rotation, updates tracking angle
// - Opcode 0x68/0x69: Likely related tracking system operations (FUN_00257f18/f78)
//
// Returns: void (no return value)
//
// Original signature: void FUN_0025fa40(void)

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Select current entity frame by index or direct pointer
extern void select_current_object_frame(uint32_t selector, void *fallbackPtr);

// Position/rotation normalization scale factor
// Maps script integer coordinates to world-space float positions
extern float fGpffff8c70;

// Current selected entity pointer
extern uint32_t uGpffffb0d4;

// Entity tracking/rotation update function
// Calculates angle to target position, updates entity rotation based on tracking mode
// param_1: target X position (normalized float)
// param_2: target Z position (normalized float)
// param_3: entity pointer
// param_4: additional mode/config parameter
extern void FUN_00257c78(float target_x, float target_z, void *entity, uint32_t mode_param);

// Analyzed implementation
void opcode_0x67_set_entity_target_position_with_rotation(void)
{
  uint32_t saved_entity;
  int32_t params[4];  // Stack offsets: +0x40, +0x3C, +0x38, +0x34
  int32_t position_x; // Stack offset: +0x30
  int32_t position_z; // Stack offset: +0x2C

  // Save current entity pointer
  saved_entity = uGpffffb0d4;

  // Read 6 script parameters via bytecode evaluator
  bytecode_interpreter(&params[0]);  // Param 0: entity selector
  bytecode_interpreter(&params[1]);  // Param 1: (passed to FUN_00257c78 as param4)
  bytecode_interpreter(&params[2]);  // Param 2: unused in this opcode
  bytecode_interpreter(&params[3]);  // Param 3: unused in this opcode
  bytecode_interpreter(&position_x); // Param 4: target X coordinate (int)
  bytecode_interpreter(&position_z); // Param 5: target Z coordinate (int)

  // Select target entity using param 0 as selector
  select_current_object_frame(params[0], saved_entity);

  // Normalize position coordinates to world-space floats and call tracking update
  // This sets the target position and updates entity rotation based on tracking mode
  FUN_00257c78(
      (float)position_x / fGpffff8c70, // Normalized target X
      (float)position_z / fGpffff8c70, // Normalized target Z
      uGpffffb0d4,                     // Selected entity pointer
      params[1]                        // Additional mode/config parameter
  );

  return;
}

// Original signature wrapper
void FUN_0025fa40(void)
{
  opcode_0x67_set_entity_target_position_with_rotation();
}
