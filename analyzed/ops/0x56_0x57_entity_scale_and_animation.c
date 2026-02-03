// analyzed/ops/0x56_0x57_entity_scale_and_animation.c
// Original: FUN_0025efa8 (0x56), FUN_0025f010 (0x57)
// Opcodes: 0x56, 0x57
// Handlers: Entity scale and animation parameter configuration

// Opcode 0x56: set_entity_scale
// Behavior:
// - Saves current entity pointer (uGpffffb0d4) as fallback.
// - Evaluates 2 expressions: entity selector, scale value.
// - Selects entity via FUN_0025d6c0 (selector, saved pointer).
// - Normalizes scale by fGpffff8c44.
// - Sets entity scale via FUN_00229ef0(normalized_scale, entity).
// - Returns 0.

// Opcode 0x57: configure_entity_animation_params
// Behavior:
// - Saves current entity pointer (iGpffffb0d4) as fallback.
// - Evaluates 3 expressions: entity selector, mode, value.
// - Selects entity via FUN_0025d6c0 (selector, saved pointer).
// - If mode == 0:
//     Sets entity scale to 1.0 via FUN_00229ef0(1.0, entity).
// - Else (mode != 0):
//     Calculates animation parameters:
//     - entity[+0x144] = value / fGpffff8c48
//     - entity[+0x148] = entity[+0x58] * (mode / fGpffff8c48) + fGpffff8c50
//     - entity[+0x140] = entity[+0x54] * (mode / fGpffff8c48) + fGpffff8c4c
// - Returns 0.

// Related:
// - FUN_0025d6c0: select_current_object_frame (analyzed/select_current_object_frame.c)
// - FUN_00229ef0: Set entity scale and related parameters
//   - Sets entity[+0x14C] = scale (stored at +0xA6 in u16* view)
//   - Sets entity[+0x150] = scale (stored at +0xA8 in u16* view)
//   - Multiplies scale by descriptor[+0x08] value
//   - Updates entity bounding box/collision parameters
// - fGpffff8c44: Scale normalization divisor for 0x56
// - fGpffff8c48: Animation parameter normalization divisor for 0x57
// - fGpffff8c4c, fGpffff8c50: Animation base offsets for 0x57

// Entity Offsets:
// - +0x54, +0x58: Base animation parameters (referenced by 0x57)
// - +0x140: Animation parameter 1 (calculated by 0x57)
// - +0x144: Animation parameter 2 (calculated by 0x57)
// - +0x148: Animation parameter 3 (calculated by 0x57)
// - +0x14C: Entity scale X (set by FUN_00229ef0, at +0xA6 as u16*)
// - +0x150: Entity scale Y (set by FUN_00229ef0, at +0xA8 as u16*)

// Usage Context:
// - Opcode 0x56: Simple uniform scale setter (models, billboards, sprites)
// - Opcode 0x57: Complex animation configuration (skeletal anim, blending)
//   - mode == 0: Reset to default scale (1.0)
//   - mode != 0: Configure animation blending/interpolation parameters
//   - Uses entity's base parameters (+0x54/58) as input
//   - Computes interpolation weights (+0x140/144/148)

// Animation Parameter Semantics (0x57):
// - entity[+0x54/58]: Base animation state (blend weights, frame indices)
// - entity[+0x140]: Computed blend weight or interpolation factor
// - entity[+0x144]: Time/frame offset for animation
// - entity[+0x148]: Computed animation position (Y-axis blend)
// - fGpffff8c48: Time/frame normalization factor
// - fGpffff8c4c/50: Position offsets for animation space

// PS2 Architecture:
// - Scale affects vertex transformation in VU pipeline
// - Animation parameters used for skeletal blending (VU1 skinning)
// - Normalization factors likely 60.0 or 30.0 (frame rate dependent)
// - Base offsets convert from gameplay space to animation space

#include <stdint.h>

// External declarations
typedef void (*bytecode_evaluator_t)(void *);
extern bytecode_evaluator_t FUN_0025c258; // Bytecode expression evaluator

// Entity selection: sets DAT_00355044 based on selector or fallback pointer
extern void FUN_0025d6c0(uint32_t selector, uint32_t fallback_ptr);

// Entity scale setter: configures scale and bounding box
// - param_1: scale factor (float)
// - param_2: entity pointer
// - Sets entity[+0x14C] = entity[+0x150] = scale
// - Multiplies by descriptor size, updates collision bounds
extern void FUN_00229ef0(float scale, uint64_t entity_ptr);

// Globals
extern uint32_t uGpffffb0d4; // Current selected entity pointer (0x56)
extern int32_t iGpffffb0d4;  // Current selected entity pointer (0x57, int view)

// Normalization factors
extern float fGpffff8c44; // Scale divisor for 0x56
extern float fGpffff8c48; // Animation parameter divisor for 0x57
extern float fGpffff8c4c; // Animation base offset 1 for 0x57
extern float fGpffff8c50; // Animation base offset 2 for 0x57

// Opcode 0x56: Set entity scale
uint64_t opcode_0x56_set_entity_scale(void)
{
  uint32_t saved_entity_ptr;
  uint32_t vm_result[4];
  int32_t selector;
  int32_t scale_int;
  float scale_normalized;

  // Save current entity pointer as fallback
  saved_entity_ptr = uGpffffb0d4;

  // Evaluate entity selector expression
  FUN_0025c258(vm_result);
  selector = (int32_t)vm_result[0];

  // Evaluate scale value expression
  FUN_0025c258(&vm_result[1]);
  scale_int = (int32_t)vm_result[1];

  // Select entity (by index if selector < 0x100, else use saved pointer)
  FUN_0025d6c0((uint32_t)selector, saved_entity_ptr);

  // Normalize scale and apply to entity
  scale_normalized = (float)scale_int / fGpffff8c44;
  FUN_00229ef0(scale_normalized, (uint64_t)uGpffffb0d4);

  return 0;
}

// Opcode 0x57: Configure entity animation parameters
uint64_t opcode_0x57_configure_entity_animation_params(void)
{
  int32_t saved_entity_ptr;
  uint32_t vm_result[4];
  int32_t selector;
  int32_t mode;
  int32_t value;
  int32_t entity_ptr;

  // Save current entity pointer as fallback
  saved_entity_ptr = iGpffffb0d4;

  // Evaluate entity selector expression
  FUN_0025c258(vm_result);
  selector = (int32_t)vm_result[0];

  // Evaluate mode expression
  FUN_0025c258(&vm_result[1]);
  mode = (int32_t)vm_result[1];

  // Evaluate value expression
  FUN_0025c258(&vm_result[2]);
  value = (int32_t)vm_result[2];

  // Select entity (by index if selector < 0x100, else use saved pointer)
  FUN_0025d6c0((uint32_t)selector, (uint32_t)saved_entity_ptr);

  entity_ptr = iGpffffb0d4;

  if (mode == 0)
  {
    // Reset mode: set scale to 1.0 (0x3f800000 in IEEE 754)
    FUN_00229ef0(1.0f, (uint64_t)entity_ptr);
  }
  else
  {
    // Animation configuration mode: compute interpolation parameters

    // Set animation time/frame offset
    *(float *)(entity_ptr + 0x144) = (float)value / fGpffff8c48;

    // Compute animation Y position (blend with entity base param +0x58)
    *(float *)(entity_ptr + 0x148) =
        *(float *)(entity_ptr + 0x58) * ((float)mode / fGpffff8c48) + fGpffff8c50;

    // Compute animation X position (blend with entity base param +0x54)
    *(float *)(entity_ptr + 0x140) =
        *(float *)(entity_ptr + 0x54) * ((float)mode / fGpffff8c48) + fGpffff8c4c;
  }

  return 0;
}

/*
 * Function Call Hierarchies:
 *
 * opcode_0x56_set_entity_scale()
 *   ├─> saved = uGpffffb0d4                    [Save current entity]
 *   ├─> FUN_0025c258(&selector)                [Eval entity selector]
 *   ├─> FUN_0025c258(&scale_int)               [Eval scale value]
 *   ├─> FUN_0025d6c0(selector, saved)          [Select entity]
 *   └─> FUN_00229ef0(scale/divisor, entity)    [Set entity scale]
 *         ├─> entity[+0x14C] = scale           [Set X scale]
 *         ├─> entity[+0x150] = scale           [Set Y scale]
 *         ├─> scaled = scale * descriptor[+8]  [Apply descriptor size]
 *         └─> [Update bounding box]            [Collision bounds]
 *
 * opcode_0x57_configure_entity_animation_params()
 *   ├─> saved = iGpffffb0d4                    [Save current entity]
 *   ├─> FUN_0025c258(&selector)                [Eval entity selector]
 *   ├─> FUN_0025c258(&mode)                    [Eval mode]
 *   ├─> FUN_0025c258(&value)                   [Eval value]
 *   ├─> FUN_0025d6c0(selector, saved)          [Select entity]
 *   └─> if (mode == 0)
 *         └─> FUN_00229ef0(1.0, entity)        [Reset scale to 1.0]
 *       else
 *         ├─> entity[+0x144] = value/norm      [Set time offset]
 *         ├─> entity[+0x148] = base*weight+off [Calc Y position]
 *         └─> entity[+0x140] = base*weight+off [Calc X position]
 *
 * Typical Script Sequences:
 * ```
 * # Sequence 1: Scale entity to 50% size
 * 0x56 [entity_idx] [50]     # Set scale = 50/fGpffff8c44 (likely ~0.5)
 *
 * # Sequence 2: Reset entity scale
 * 0x57 [entity_idx] 0 0      # mode=0: reset scale to 1.0
 *
 * # Sequence 3: Configure animation blending
 * 0x57 [entity_idx] [blend_mode] [frame_offset]  # mode!=0: setup animation
 *
 * # Sequence 4: Dynamic scaling effect
 * # (scale entity over time in loop)
 * :loop
 *   0x1E [scale_var] 1       # increment scale
 *   0x56 [entity] [scale_var] # apply scale
 *   0x33                      # sync frame
 *   0x14 [scale_var] 100      # if scale < 100
 *   0x0E :loop                # goto loop
 * ```
 *
 * Scale Normalization (0x56):
 * - fGpffff8c44 likely = 100.0 (percent scale)
 *   - Input: 50 → Output: 0.5 (50% scale)
 *   - Input: 100 → Output: 1.0 (100% scale)
 *   - Input: 200 → Output: 2.0 (200% scale)
 * - Allows intuitive integer scale values in scripts
 * - FUN_00229ef0 multiplies by descriptor size for absolute units
 *
 * Animation Parameters (0x57):
 * - entity[+0x54/58]: Base animation weights/positions
 *   - Set during entity initialization
 *   - Represent default/rest pose
 * - entity[+0x140/144/148]: Computed interpolation targets
 *   - Updated each frame by animation system
 *   - Used for skeletal blending (VU1)
 * - mode: Blend mode/weight
 *   - 0: Reset to default (scale=1.0)
 *   - >0: Interpolation weight (typically 0-100)
 * - value: Animation time/frame offset
 *   - Used for syncing animations
 *   - Normalized by fGpffff8c48 (likely frame rate)
 *
 * Entity Structure (relevant offsets):
 * +0x00: Type ID
 * +0x04: Status flags
 * +0x20-0x2B: Position XYZ
 * +0x4C: Tag/index
 * +0x54: Animation base X
 * +0x58: Animation base Y
 * ...
 * +0x140: Animation computed X
 * +0x144: Animation computed time
 * +0x148: Animation computed Y
 * +0x14C: Scale X (float)
 * +0x150: Scale Y (float)
 *
 * FUN_00229ef0 Implementation:
 * 1. Validate entity via FUN_00229980 (descriptor lookup)
 * 2. Set entity[+0x14C] = entity[+0x150] = scale (stored as float)
 * 3. Multiply scale by descriptor[+0x08] (base size from type)
 * 4. Update entity bounding box dimensions
 * 5. Recalculate collision detection bounds
 *
 * Performance Notes:
 * - Scale changes require bounding box recalculation
 * - Animation parameter updates are cheap (direct writes)
 * - VU1 reads these parameters for vertex transformation
 * - Per-frame updates common for animated entities
 *
 * Use Cases:
 * 0x56 (set_entity_scale):
 * - Boss size increase during phase transitions
 * - Item/pickup pulse animation (scale oscillation)
 * - Distance-based LOD scaling
 * - UI element resize (menus, HUD)
 * - Power-up visual feedback (grow/shrink)
 *
 * 0x57 (configure_entity_animation_params):
 * - Skeletal animation blending (walk→run transition)
 * - Weapon swing animation timing
 * - Character facial expressions
 * - Cloth/cape physics integration
 * - Particle effect animation sync
 *
 * Comparison with Related Opcodes:
 * - 0x54/0x55: Set entity position (XYZ)
 * - 0x56: Set entity scale (uniform)
 * - 0x57: Configure animation parameters (complex)
 * - 0x64: Update transform from bone (skeletal)
 * - 0x77-0x7C: Modify entity registers (RMW ops)
 *
 * Debug Considerations:
 * - Invalid scale (0.0, negative) may cause division by zero
 * - Very large scales (>10.0) may exceed render bounds
 * - Animation parameters outside [0,1] may produce artifacts
 * - Mode=0 in 0x57 acts as safety reset
 *
 * Animation System Integration:
 * - Base parameters (+0x54/58) set by entity initialization
 * - Computed parameters (+0x140/144/148) updated by script
 * - VU1 animation code reads both for blending
 * - Final vertex positions computed in VU1 pipeline
 * - Per-frame update cycle: script→CPU→VU1→GS
 *
 * Scale vs. Animation:
 * - Scale (0x56): Uniform size change, instant effect
 * - Animation (0x57): Per-vertex deformation, frame interpolated
 * - Both can be used together for complex effects
 * - Scale applied after animation in transform pipeline
 *
 * Error Handling:
 * - FUN_00229ef0 validates entity via FUN_00229980
 * - Invalid entity types trigger debug error
 * - No explicit bounds checking on scale values
 * - Negative scales may flip rendering (implementation-defined)
 *
 * Cross-References:
 * - analyzed/ops/0x54_0x55_set_entity_position.c: Position setters
 * - analyzed/ops/0x58_select_pw_slot_by_index.c: Entity selection
 * - analyzed/ops/0x64_update_object_transform_from_bone.c: Bone transforms
 * - analyzed/setup_camera_entity.c: FUN_00229ef0 usage (scale=12.0)
 * - src/FUN_00229ef0.c: Scale setter implementation
 * - src/FUN_00229980.c: Entity descriptor validation
 *
 * Normalization Factor Values (estimated):
 * - fGpffff8c44: ~100.0 (percent scale, 0x42C80000)
 * - fGpffff8c48: ~60.0 or ~30.0 (frame rate, 0x42700000 or 0x41F00000)
 * - fGpffff8c4c: Unknown base offset (likely small, <10.0)
 * - fGpffff8c50: Unknown base offset (likely small, <10.0)
 *
 * Additional Notes:
 * - Entity pool stride 0xEC (236 bytes) consistent across opcodes
 * - Scale stored as float, not fixed-point (PS2 FPU available)
 * - Animation parameters suggest quaternion or matrix interpolation
 * - Mode parameter in 0x57 likely blend weight (0-100 scale)
 * - Value parameter in 0x57 likely frame index or time offset
 * - Base offsets (+0x54/58) set during spawn, rarely changed
 * - Computed offsets (+0x140/144/148) updated frequently
 */
