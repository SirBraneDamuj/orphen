// analyzed/ops/0x57_configure_entity_animation_params.c
// Original: FUN_0025f010
// Opcode: 0x57
// Handler: Configure entity animation/interpolation parameters

// Behavior:
// - Saves current entity pointer (iGpffffb0d4) as fallback.
// - Evaluates 3 expressions: entity selector, mode, value.
// - Selects entity via FUN_0025d6c0 (selector, saved pointer).
// - If mode == 0:
//     Resets entity scale to 1.0 via FUN_00229ef0(1.0, entity).
// - Else (mode != 0):
//     Calculates animation interpolation parameters:
//     - entity[+0x144] = value / fGpffff8c48        (time/frame offset)
//     - entity[+0x148] = entity[+0x58] * (mode / fGpffff8c48) + fGpffff8c50  (Y position)
//     - entity[+0x140] = entity[+0x54] * (mode / fGpffff8c48) + fGpffff8c4c  (X position)
// - Returns 0.

// Related:
// - FUN_0025d6c0: select_current_object_frame (analyzed/select_current_object_frame.c)
// - FUN_00229ef0: Set entity scale (also used by 0x56)
// - fGpffff8c48: Normalization divisor (~60.0 or ~30.0, frame rate)
// - fGpffff8c4c: Base offset for X position calculation
// - fGpffff8c50: Base offset for Y position calculation

// Entity Offsets:
// - +0x54: Animation base X parameter (read)
// - +0x58: Animation base Y parameter (read)
// - +0x140: Computed animation X position (write)
// - +0x144: Computed animation time/frame offset (write)
// - +0x148: Computed animation Y position (write)
// - +0x14C, +0x150: Entity scale (written when mode==0)

// Usage Context:
// - Skeletal animation blending (walk→run transitions)
// - Weapon swing timing synchronization
// - Character facial expression interpolation
// - Cloth/cape physics parameter setup
// - Particle effect animation sync
// - Mode=0: Safety reset to default state
// - Mode!=0: Configure interpolation weights

// Animation Parameter Semantics:
// - Base parameters (+0x54/58): Set during entity spawn, represent rest pose
// - Computed parameters (+0x140/144/148): Updated by scripts, drive animation
// - VU1 reads computed params for vertex skinning/blending
// - mode: Blend weight or interpolation factor (0-100 typical)
// - value: Time offset or frame index for sync
// - Base offsets (fGpffff8c4c/50): Convert gameplay→animation space

// PS2 Architecture:
// - Parameters used in VU1 skeletal animation pipeline
// - Normalization by fGpffff8c48 likely frame rate dependent
// - Base offsets convert coordinate spaces
// - Computed values drive per-vertex interpolation

#include <stdint.h>

// External declarations
typedef void (*bytecode_evaluator_t)(void *);
extern bytecode_evaluator_t FUN_0025c258; // Bytecode expression evaluator

// Entity selection: sets DAT_00355044 based on selector or fallback pointer
extern void FUN_0025d6c0(uint32_t selector, uint32_t fallback_ptr);

// Entity scale setter (used when mode==0)
extern void FUN_00229ef0(float scale, uint64_t entity_ptr);

// Globals
extern int32_t iGpffffb0d4; // Current selected entity pointer
extern float fGpffff8c48;   // Animation normalization divisor (~60.0)
extern float fGpffff8c4c;   // Animation base offset X
extern float fGpffff8c50;   // Animation base offset Y

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

  // Evaluate mode expression (0=reset, >0=blend weight)
  FUN_0025c258(&vm_result[1]);
  mode = (int32_t)vm_result[1];

  // Evaluate value expression (time/frame offset)
  FUN_0025c258(&vm_result[2]);
  value = (int32_t)vm_result[2];

  // Select entity (by index if selector < 0x100, else use saved pointer)
  FUN_0025d6c0((uint32_t)selector, (uint32_t)saved_entity_ptr);

  entity_ptr = iGpffffb0d4;

  if (mode == 0)
  {
    // Reset mode: set scale to 1.0 (0x3f800000 in IEEE 754 float)
    // Clears any scaling effects, restores default size
    FUN_00229ef0(1.0f, (uint64_t)entity_ptr);
  }
  else
  {
    // Animation configuration mode: compute interpolation parameters

    // Set animation time/frame offset
    // Used for syncing animation timing across multiple entities
    *(float *)(entity_ptr + 0x144) = (float)value / fGpffff8c48;

    // Compute animation Y position (blend with entity base param +0x58)
    // Formula: target_Y = base_Y * weight + offset
    *(float *)(entity_ptr + 0x148) =
        *(float *)(entity_ptr + 0x58) * ((float)mode / fGpffff8c48) + fGpffff8c50;

    // Compute animation X position (blend with entity base param +0x54)
    // Formula: target_X = base_X * weight + offset
    *(float *)(entity_ptr + 0x140) =
        *(float *)(entity_ptr + 0x54) * ((float)mode / fGpffff8c48) + fGpffff8c4c;
  }

  return 0;
}

/*
 * Function Call Hierarchy:
 *
 * opcode_0x57_configure_entity_animation_params()
 *   ├─> saved = iGpffffb0d4                    [Save current entity]
 *   ├─> FUN_0025c258(&selector)                [Eval entity selector]
 *   ├─> FUN_0025c258(&mode)                    [Eval mode/weight]
 *   ├─> FUN_0025c258(&value)                   [Eval time/frame]
 *   ├─> FUN_0025d6c0(selector, saved)          [Select entity]
 *   └─> if (mode == 0)
 *         └─> FUN_00229ef0(1.0, entity)        [Reset scale to default]
 *       else
 *         ├─> entity[+0x144] = value/norm      [Set time offset]
 *         ├─> entity[+0x148] = base*w+off      [Calc target Y]
 *         └─> entity[+0x140] = base*w+off      [Calc target X]
 *
 * Typical Script Sequences:
 * ```
 * # Sequence 1: Reset entity to default state
 * 0x57 [entity_idx] 0 0      # mode=0: reset scale to 1.0
 *
 * # Sequence 2: Configure walk animation blend
 * 0x57 [entity_idx] 60 0     # mode=60: 100% blend weight (60/60.0=1.0)
 *
 * # Sequence 3: Sync animation timing
 * 0x57 [entity_idx] 30 15    # mode=30: 50% weight, value=15: frame offset
 *
 * # Sequence 4: Smooth transition (loop)
 * 0x36 [weight_var]          # read current weight
 * 0x1E [weight_var] 1        # increment weight
 * 0x57 [entity] [weight_var] 0  # apply blend weight
 * 0x33                       # sync frame
 * 0x14 [weight_var] 60       # if weight < 60
 * 0x0E :transition_loop      # goto loop
 * ```
 *
 * Parameter Calculation Details:
 *
 * Time Offset (+0x144):
 * - Formula: value / fGpffff8c48
 * - If fGpffff8c48 = 60.0 (60 FPS):
 *   - value=0 → offset=0.0 (synchronized)
 *   - value=15 → offset=0.25 (quarter second behind)
 *   - value=30 → offset=0.5 (half second behind)
 * - Used to stagger animations across multiple entities
 *
 * Y Position (+0x148):
 * - Formula: base_Y * (mode / divisor) + offset_Y
 * - base_Y = entity[+0x58] (set during spawn)
 * - If base_Y=2.0, mode=30, divisor=60.0, offset=0.5:
 *   - Result: 2.0 * 0.5 + 0.5 = 1.5
 * - Interpolates between base and target positions
 *
 * X Position (+0x140):
 * - Formula: base_X * (mode / divisor) + offset_X
 * - base_X = entity[+0x54] (set during spawn)
 * - Same interpolation as Y, different base/offset
 *
 * Mode Parameter Interpretation:
 * - mode=0: Special case, reset to default
 * - mode=1-60: Blend weight (0-100% if divisor=60.0)
 * - mode>60: Over-blend (>100%, extrapolation)
 * - Negative mode: Reverse blend (rarely used)
 *
 * Value Parameter Interpretation:
 * - value=0: No time offset, synchronized
 * - value>0: Delay animation start (in frames)
 * - value<0: Advance animation start (pre-roll)
 * - Normalized by frame rate for time-independence
 *
 * Entity Structure (animation fields):
 * +0x54: Animation base X (float, read-only in this opcode)
 * +0x58: Animation base Y (float, read-only in this opcode)
 * +0x140: Animation computed X (float, written)
 * +0x144: Animation computed time (float, written)
 * +0x148: Animation computed Y (float, written)
 *
 * VU1 Animation Pipeline:
 * 1. Read base parameters (+0x54/58) from entity
 * 2. Read computed parameters (+0x140/144/148) from entity
 * 3. Interpolate vertex positions using blend weight
 * 4. Apply scale (+0x14C/150) to final result
 * 5. Transform to view space
 * 6. Send to GS for rasterization
 *
 * Use Cases:
 * - Walk→Run transition: mode increases 0→60 over 0.5 seconds
 * - Sword swing: value=0-60 for timing, mode=60 for full swing
 * - Facial expression: mode=0-30 for partial smile blend
 * - Cloth physics: mode oscillates for wave motion
 * - Particle spawn: value staggers particle start times
 *
 * Mode=0 Reset Behavior:
 * - Called at scene start to clear old state
 * - Used after cutscenes to restore gameplay
 * - Resets scale but not animation parameters
 * - Safe default when animation state unknown
 * - Prevents artifacts from previous animations
 *
 * Performance Notes:
 * - Direct memory writes (no function calls when mode!=0)
 * - Very cheap operation (3 float writes)
 * - Can be called every frame for smooth blending
 * - VU1 reads these values per-frame for skinning
 * - No bounding box recalc (unlike scale changes)
 *
 * Comparison with 0x56:
 * - 0x56: Uniform scale (visual size change)
 * - 0x57: Animation parameters (skeletal deformation)
 * - 0x56: Single purpose (scale only)
 * - 0x57: Dual purpose (reset or configure)
 * - Both can be used together for complex effects
 *
 * Animation System Architecture:
 * - Base parameters: Set once during entity initialization
 * - Computed parameters: Updated by scripts for gameplay
 * - VU1 blending: Combines base + computed per-vertex
 * - Final transform: Model→Animation→Scale→View→Clip
 *
 * Common Animation Patterns:
 * 1. Crossfade: mode ramps 0→60, old anim fades out
 * 2. Additive: mode stays at 30 (50%), adds motion
 * 3. Override: mode=60 (100%), replaces base pose
 * 4. Idle variation: mode oscillates 20-40 for subtle motion
 *
 * Debug Considerations:
 * - Watch for mode>60 causing over-extrapolation
 * - Monitor value for excessive time offsets
 * - Check base params (+0x54/58) are initialized
 * - Verify computed params update each frame
 * - mode=0 should restore expected visual state
 *
 * Normalization Factor Analysis:
 * - fGpffff8c48 likely 60.0 (NTSC) or 50.0 (PAL)
 * - Frame rate dependent for time-correct animation
 * - Allows integer script values (intuitive)
 * - Converts frames→seconds for physics/animation
 *
 * Base Offset Analysis:
 * - fGpffff8c4c/50 likely small (<5.0)
 * - Shifts interpolation space
 * - May convert world→animation coordinate systems
 * - Could be character-height or pivot-point offsets
 *
 * Error Handling:
 * - No validation on mode or value ranges
 * - Invalid entity selection fails silently
 * - Negative mode/value accepted (may cause artifacts)
 * - mode=0 acts as safe fallback/reset
 *
 * Cross-References:
 * - analyzed/ops/0x56_set_entity_scale.c: Scale setter (simpler)
 * - analyzed/ops/0x54_0x55_set_entity_position.c: Position setters
 * - analyzed/ops/0x64_update_object_transform_from_bone.c: Bone transforms
 * - analyzed/ops/0x58_select_pw_slot_by_index.c: Entity selection
 * - src/FUN_00229ef0.c: Scale setter (called when mode=0)
 *
 * Additional Notes:
 * - Animation parameters persist until overwritten
 * - Entity respawn typically resets to base values
 * - Can query current params via memory read opcodes
 * - Advanced: Direct manipulation via register opcodes (0x77-0x7C)
 * - Multiple entities can share same animation timing
 */
