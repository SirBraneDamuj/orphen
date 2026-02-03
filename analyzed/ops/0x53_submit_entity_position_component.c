// analyzed/ops/0x53_submit_entity_position_component.c
// Original: FUN_0025ee08
// Opcode: 0x53
// Handler: Submit entity position component (X/Y/Z) scaled to parameter stack

// Behavior:
// - Evaluates two expressions: entity index, axis selector (0=X, 1=Y, 2=Z).
// - Selects entity via FUN_0025d6c0 (index or current if 0x100).
// - Reads position component from iGpffffb0d4 entity pointer:
//   - axis 0: entity+0x20 (X) scaled by fGpffff8c34
//   - axis 1: entity+0x24 (Y) scaled by fGpffff8c38
//   - axis 2: entity+0x28 (Z) scaled by fGpffff8c3c
// - Submits scaled value to parameter stack via FUN_0030bd20.
// - Returns 0 if axis selector invalid (not 0/1/2).

// Related:
// - 0x54/0x55: set_entity_position (writes position components)
// - 0x70: submit_angle_to_target (computes angle between entities)
// - 0x71: submit_distance_to_target (computes distance between entities)
// - FUN_0025d6c0: Entity selector (sets DAT_00355044 and returns entity pointer)
// - FUN_0030bd20: Parameter stack submitter

// PS2 Architecture:
// - Entity position stored as three floats at offsets +0x20, +0x24, +0x28.
// - Each axis has independent normalization scale factor.
// - Coordinate system likely Y-up (axis 1 = vertical height).

#include <stdint.h>

// External declarations
extern int32_t iGpffffb0d4; // Current entity pointer (set by selector)
typedef void (*bytecode_evaluator_t)(void *);
extern bytecode_evaluator_t FUN_0025c258;                      // Bytecode expression evaluator
extern void FUN_0025d6c0(int32_t index, int32_t fallback_ptr); // Entity selector
extern uint64_t FUN_0030bd20(float value);                     // Parameter stack submitter

// Coordinate scale factors (entity space -> parameter space)
extern float fGpffff8c34; // X-axis scale factor
extern float fGpffff8c38; // Y-axis scale factor
extern float fGpffff8c3c; // Z-axis scale factor

// Entity structure offsets
#define ENTITY_POS_X_OFFSET 0x20 // Float X coordinate
#define ENTITY_POS_Y_OFFSET 0x24 // Float Y coordinate
#define ENTITY_POS_Z_OFFSET 0x28 // Float Z coordinate

uint64_t opcode_0x53_submit_entity_position_component(void)
{
  int32_t entity_index;
  int32_t axis_selector;
  float position_component;
  float scale_factor;
  uint64_t result;

  // Backup current entity pointer
  int32_t entity_ptr = iGpffffb0d4;

  // Evaluate two expressions: entity index and axis selector
  FUN_0025c258(&entity_index);
  FUN_0025c258(&axis_selector);

  // Select entity by index (updates DAT_00355044)
  // If index==0x100, uses entity_ptr as fallback
  FUN_0025d6c0(entity_index, entity_ptr);

  // Read position component based on axis selector
  if (axis_selector == 1)
  {
    // Y-axis (vertical)
    position_component = *(float *)(iGpffffb0d4 + ENTITY_POS_Y_OFFSET);
    scale_factor = fGpffff8c38;
  }
  else if (axis_selector < 2)
  {
    if (axis_selector != 0)
    {
      return 0; // Invalid selector
    }
    // X-axis (horizontal)
    position_component = *(float *)(iGpffffb0d4 + ENTITY_POS_X_OFFSET);
    scale_factor = fGpffff8c34;
  }
  else
  {
    if (axis_selector != 2)
    {
      return 0; // Invalid selector
    }
    // Z-axis (depth)
    position_component = *(float *)(iGpffffb0d4 + ENTITY_POS_Z_OFFSET);
    scale_factor = fGpffff8c3c;
  }

  // Submit scaled position to parameter stack
  result = FUN_0030bd20(position_component * scale_factor);

  return result;
}

/*
 * Function Call Hierarchy:
 *
 * opcode_0x53_submit_entity_position_component()
 *   ├─> FUN_0025c258(&entity_index)      [Eval expr: entity index]
 *   ├─> FUN_0025c258(&axis_selector)     [Eval expr: axis (0=X, 1=Y, 2=Z)]
 *   ├─> FUN_0025d6c0(index, fallback)    [Select entity, update DAT_00355044]
 *   │     └─> Sets DAT_00355044 = &DAT_0058beb0[index*0xEC] or fallback
 *   └─> FUN_0030bd20(scaled_value)       [Submit to parameter stack]
 *
 * Entity Position Layout (floats):
 * - +0x20: X coordinate (scaled by fGpffff8c34)
 * - +0x24: Y coordinate (scaled by fGpffff8c38, vertical)
 * - +0x28: Z coordinate (scaled by fGpffff8c3c)
 * - +0x4C: Z duplicate (see 0x54/0x55 analysis)
 *
 * Scale Factors:
 * - fGpffff8c34: X-axis normalization (entity -> parameter space)
 * - fGpffff8c38: Y-axis normalization (entity -> parameter space)
 * - fGpffff8c3c: Z-axis normalization (entity -> parameter space)
 * - These differ from position setter scales (fGpffff8c40 in 0x54/0x55)
 *
 * Axis Selector Values:
 * - 0: X-axis (horizontal left-right)
 * - 1: Y-axis (vertical up-down, typical PS2 convention)
 * - 2: Z-axis (depth forward-backward)
 * - Other: Invalid, returns 0
 *
 * Usage Patterns:
 * - Read entity position for conditional logic (distance checks, triggers).
 * - Used with comparison opcodes to test entity locations.
 * - Typical sequence: 0x58 (select entity) -> 0x53 (read pos) -> compare.
 * - Complements 0x54/0x55 which write positions.
 *
 * Related Opcodes:
 * - 0x54: set_entity_position (write XYZ)
 * - 0x55: set_entity_position with terrain height calc
 * - 0x58: select_pw_slot_by_index (sets entity context)
 * - 0x70: submit_angle_to_target (angle computation)
 * - 0x71: submit_distance_to_target (distance computation)
 * - 0x76: select_object_and_read_register (generic register reader)
 *
 * Example Script Usage:
 * ```
 * 0x58 [entity_idx]      # Select entity
 * 0x53 0x100 0x01        # Read Y position of selected entity
 * 0x20 [threshold]       # Push threshold value
 * 0x2A                   # Compare (less than)
 * 0x0D [offset]          # Conditional branch if Y < threshold
 * ```
 */
