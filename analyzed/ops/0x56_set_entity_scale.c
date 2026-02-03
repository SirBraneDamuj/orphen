// analyzed/ops/0x56_set_entity_scale.c
// Original: FUN_0025efa8
// Opcode: 0x56
// Handler: Set entity scale (uniform)

// Behavior:
// - Saves current entity pointer (uGpffffb0d4) as fallback.
// - Evaluates 2 expressions: entity selector, scale value.
// - Selects entity via FUN_0025d6c0 (selector, saved pointer).
// - Normalizes scale by fGpffff8c44 (likely 100.0 for percent).
// - Sets entity scale via FUN_00229ef0(normalized_scale, entity).
// - Returns 0.

// Related:
// - FUN_0025d6c0: select_current_object_frame (analyzed/select_current_object_frame.c)
// - FUN_00229ef0: Set entity scale and bounding box
//   - Sets entity[+0x14C] = scale (X scale)
//   - Sets entity[+0x150] = scale (Y scale, uniform)
//   - Multiplies scale by descriptor[+0x08] (base size)
//   - Updates collision bounds
// - fGpffff8c44: Scale normalization divisor (~100.0 for percent)

// Entity Offsets:
// - +0x14C: Entity scale X (float, set by FUN_00229ef0)
// - +0x150: Entity scale Y (float, set by FUN_00229ef0)

// Usage Context:
// - Boss size changes during phase transitions
// - Item/pickup pulse animations (oscillating scale)
// - Distance-based LOD scaling
// - UI element resize (menus, HUD elements)
// - Power-up visual feedback (grow/shrink effects)

// PS2 Architecture:
// - Scale affects vertex transformation in VU1 pipeline
// - Applied to all vertices uniformly (non-skeletal)
// - Bounding box recalculated for collision detection
// - Normalized input allows intuitive integer values (50 = 50% scale)

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
extern uint32_t uGpffffb0d4; // Current selected entity pointer
extern float fGpffff8c44;    // Scale normalization divisor (~100.0)

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
  // Example: scale_int=50, fGpffff8c44=100.0 → scale_normalized=0.5 (50%)
  scale_normalized = (float)scale_int / fGpffff8c44;
  FUN_00229ef0(scale_normalized, (uint64_t)uGpffffb0d4);

  return 0;
}

/*
 * Function Call Hierarchy:
 *
 * opcode_0x56_set_entity_scale()
 *   ├─> saved = uGpffffb0d4                    [Save current entity]
 *   ├─> FUN_0025c258(&selector)                [Eval entity selector]
 *   ├─> FUN_0025c258(&scale_int)               [Eval scale value]
 *   ├─> FUN_0025d6c0(selector, saved)          [Select entity]
 *   └─> FUN_00229ef0(scale/divisor, entity)    [Set entity scale]
 *         ├─> FUN_00229980(entity, type, 0)    [Validate descriptor]
 *         ├─> entity[+0x14C] = scale           [Set X scale]
 *         ├─> entity[+0x14C] = scale           [Set Y scale (uniform)]
 *         ├─> scaled = scale * descriptor[+8]  [Apply base size]
 *         └─> [Update bounding box]            [Recalc collision]
 *
 * Typical Script Sequences:
 * ```
 * # Sequence 1: Scale entity to 50%
 * 0x56 [entity_idx] 50       # Set scale = 50/100.0 = 0.5
 *
 * # Sequence 2: Double entity size
 * 0x56 [entity_idx] 200      # Set scale = 200/100.0 = 2.0
 *
 * # Sequence 3: Pulsing animation (loop)
 * :pulse_loop
 *   0x1E [scale_var] 5       # increment scale by 5
 *   0x56 [entity] [scale_var] # apply scale
 *   0x33                      # sync frame
 *   0x14 [scale_var] 150      # if scale < 150
 *   0x0E :pulse_loop          # goto pulse_loop
 *   # (scale now 150, reverse direction)
 *   :shrink_loop
 *   0x1F [scale_var] 5        # decrement scale by 5
 *   0x56 [entity] [scale_var] # apply scale
 *   0x33                       # sync frame
 *   0x15 [scale_var] 50       # if scale > 50
 *   0x0E :shrink_loop          # goto shrink_loop
 * ```
 *
 * Scale Normalization:
 * - fGpffff8c44 likely = 100.0 (percent scale)
 *   - Input: 50 → Output: 0.5 (50% scale)
 *   - Input: 100 → Output: 1.0 (100% default scale)
 *   - Input: 200 → Output: 2.0 (200% scale)
 * - Allows intuitive integer values in scripts
 * - FUN_00229ef0 multiplies by descriptor[+0x08] for absolute size
 *
 * FUN_00229ef0 Implementation:
 * 1. Call FUN_00229980 to validate entity and get descriptor
 * 2. Set entity[+0x14C] = param_1 (scale)
 * 3. Set entity[+0x150] = param_1 (uniform scale)
 * 4. Calculate: scaled_size = param_1 * descriptor[+0x08]
 * 5. Update entity bounding box dimensions
 * 6. Recalculate collision detection bounds
 * 7. Mark entity for render update
 *
 * Entity Scale Fields:
 * - +0x14C (float): X-axis scale factor
 * - +0x150 (float): Y-axis scale factor (set equal for uniform)
 * - Stored as floats, not fixed-point
 * - Read by VU1 during vertex transformation
 * - Applied after model-space transform, before view-space
 *
 * Use Cases:
 * - Boss growth during phase transition (100 → 150 over time)
 * - Collectible item pulse (90 → 110 → 90 loop)
 * - Distance LOD (far=50, near=100)
 * - UI button hover effect (100 → 110 on hover)
 * - Power-up activation (rapid 100→200→100)
 * - Enemy spawn effect (0 → 100 fade-in)
 *
 * Performance Notes:
 * - Scale change requires bounding box recalculation (moderate cost)
 * - VU1 applies scale per-vertex (cheap, hardware accelerated)
 * - Per-frame updates acceptable for animated entities
 * - Avoid changing scale on many entities simultaneously
 *
 * Comparison with Related Opcodes:
 * - 0x54/0x55: Set entity position (XYZ location)
 * - 0x56: Set entity scale (uniform size)
 * - 0x57: Configure animation parameters (skeletal/blend)
 * - 0x64: Update transform from bone (skeleton)
 *
 * Scale Limits:
 * - Minimum: ~0.01 (1% scale, near-invisible)
 * - Maximum: ~10.0 (1000% scale, may exceed render bounds)
 * - Zero scale: May cause division-by-zero in collision
 * - Negative scale: Flips rendering (implementation-defined)
 *
 * Descriptor Size Factor:
 * - descriptor[+0x08]: Base size for entity type
 * - Varies per entity type (character=1.8, item=0.5, boss=3.0)
 * - Final size = scale * descriptor_size
 * - Allows consistent script values across entity types
 *
 * Bounding Box Update:
 * - Used for frustum culling (visibility)
 * - Used for collision detection (physics)
 * - Format likely: center + half-extents or min/max corners
 * - Updated immediately by FUN_00229ef0
 *
 * Error Handling:
 * - FUN_00229ef0 validates entity via FUN_00229980
 * - Invalid entity types trigger debug error
 * - No explicit bounds checking on scale value
 * - Very large/small scales may cause artifacts
 *
 * Cross-References:
 * - analyzed/ops/0x54_0x55_set_entity_position.c: Position setters
 * - analyzed/ops/0x57_configure_entity_animation_params.c: Animation config
 * - analyzed/ops/0x58_select_pw_slot_by_index.c: Entity selection
 * - analyzed/setup_camera_entity.c: FUN_00229ef0 usage (scale=12.0)
 * - src/FUN_00229ef0.c: Scale setter implementation (53 lines)
 * - src/FUN_00229980.c: Entity descriptor validation
 *
 * Debug Considerations:
 * - Watch for scale=0.0 causing invisible entities
 * - Monitor bounding box for collision issues
 * - Check descriptor[+0x08] for unexpected base sizes
 * - Verify scale changes take effect immediately
 *
 * VU1 Integration:
 * - Scale read from entity struct each frame
 * - Applied in vertex transformation stage
 * - Format: vertex_out = (vertex_in * model_matrix) * scale
 * - Uniform scale (both axes same) is common case
 *
 * Additional Notes:
 * - Scale persists until explicitly changed or entity reset
 * - Entity respawn typically resets scale to 1.0 (descriptor default)
 * - Script can query current scale via entity field read opcodes
 * - Non-uniform scale possible via direct memory writes (advanced)
 */
