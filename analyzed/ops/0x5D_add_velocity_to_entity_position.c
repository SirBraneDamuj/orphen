// analyzed/ops/0x5D_add_velocity_to_entity_position.c
// Original: FUN_0025f290
// Opcode: 0x5D
// Handler: Add velocity to entity position (polar coordinates)

// Behavior:
// - Saves current entity pointer (DAT_00355044) as fallback.
// - Evaluates 3 expressions: entity selector, speed, angle.
// - Selects entity via FUN_0025d6c0 (selector, saved pointer).
// - Converts polar velocity (speed, angle) to Cartesian (dx, dz):
//     speed_scaled = (speed / DAT_00352bc8) * 0.03125 * DAT_003555bc
//     angle_fixed = float_to_fixed(angle / DAT_00352bc8)
//     dx = speed_scaled * cos(angle_fixed)
//     dz = speed_scaled * sin(angle_fixed)
// - Adds velocity to entity position:
//     entity[+0x30] += dx (X position)
//     entity[+0x34] += dz (Z position)
// - Returns 0.

// Related:
// - FUN_0025d6c0: select_current_object_frame (analyzed/select_current_object_frame.c)
// - FUN_00216690: float_to_fixed_point (converts float to fixed-point for trig)
// - FUN_00305130: cos(fixed_point) → float (cosine, fixed-point input)
// - FUN_00305218: sin(fixed_point) → float (sine, fixed-point input)
// - DAT_00352bc8: Angle/speed normalization divisor
// - DAT_003555bc: Frame time delta (updates per frame)

// Entity Offsets:
// - +0x30: Entity X position (float)
// - +0x34: Entity Z position (float)
// Note: Y position (+0x34 in some contexts) is NOT modified (horizontal movement only)

// Usage Context:
// - Entity movement in 2D horizontal plane (top-down perspective)
// - Angle-based movement (0° = +X, 90° = +Z typical)
// - Speed determines magnitude, angle determines direction
// - Frame-rate independent via DAT_003555bc multiplication
// - Common for: AI pathfinding, projectiles, scripted movement

// Coordinate System:
// - X axis: Horizontal (left/right)
// - Z axis: Depth (forward/back)
// - Y axis: Vertical (up/down, not affected by this opcode)
// - Polar: (speed, angle) → Cartesian: (dx, dz)

// PS2 Architecture:
// - Uses hardware sin/cos (FPU or VU0 trig functions)
// - Fixed-point angle representation for lookup tables
// - Frame delta ensures consistent speed across frame rates
// - Direct position modification (no physics integration)

#include <stdint.h>

// External declarations
typedef void (*bytecode_evaluator_t)(void *);
extern bytecode_evaluator_t FUN_0025c258; // Bytecode expression evaluator

// Entity selection: sets DAT_00355044 based on selector or fallback pointer
extern void FUN_0025d6c0(uint32_t selector, int32_t fallback_ptr);

// Trigonometric functions (PS2 hardware accelerated)
extern uint32_t FUN_00216690(float angle);     // Convert float angle to fixed-point
extern double FUN_00305130(uint32_t angle_fp); // cos(fixed_point) → float
extern double FUN_00305218(uint32_t angle_fp); // sin(fixed_point) → float

// Globals
extern int32_t DAT_00355044; // Current selected entity pointer
extern float DAT_00352bc8;   // Angle/speed normalization divisor
extern int32_t DAT_003555bc; // Frame time delta (ticks per frame)

// Opcode 0x5D: Add velocity to entity position
uint64_t opcode_0x5D_add_velocity_to_entity_position(void)
{
  int32_t saved_entity_ptr;
  uint32_t vm_result[4];
  int32_t selector;
  int32_t speed_int;
  int32_t angle_int;
  float speed_scaled;
  uint32_t angle_fixed;
  int32_t entity_ptr_1;
  float cos_value;
  int32_t entity_ptr_2;
  float sin_value;

  // Save current entity pointer as fallback
  saved_entity_ptr = DAT_00355044;

  // Evaluate entity selector expression
  FUN_0025c258(vm_result);
  selector = (int32_t)vm_result[0];

  // Evaluate speed expression
  FUN_0025c258(&vm_result[1]);
  speed_int = (int32_t)vm_result[1];

  // Evaluate angle expression
  FUN_0025c258(&vm_result[2]);
  angle_int = (int32_t)vm_result[2];

  // Select entity (by index if selector < 0x100, else use saved pointer)
  FUN_0025d6c0((uint32_t)selector, saved_entity_ptr);

  // Calculate scaled speed (frame-rate adjusted)
  // 0.03125 = 1/32 (likely conversion factor from game units to world units)
  speed_scaled = ((float)speed_int / DAT_00352bc8) * 0.03125f * (float)DAT_003555bc;

  // Convert angle to fixed-point for trig lookup
  angle_fixed = FUN_00216690((float)angle_int / DAT_00352bc8);

  // Get entity pointer and calculate X velocity component
  entity_ptr_1 = DAT_00355044;
  cos_value = (float)FUN_00305130(angle_fixed);

  // Get entity pointer again and calculate Z velocity component
  entity_ptr_2 = DAT_00355044;

  // Add velocity to entity X position
  *(float *)(entity_ptr_1 + 0x30) = *(float *)(entity_ptr_1 + 0x30) + speed_scaled * cos_value;

  // Calculate sin for Z component
  sin_value = (float)FUN_00305218(angle_fixed);

  // Add velocity to entity Z position
  *(float *)(entity_ptr_2 + 0x34) = *(float *)(entity_ptr_2 + 0x34) + speed_scaled * sin_value;

  return 0;
}

/*
 * Function Call Hierarchy:
 *
 * opcode_0x5D_add_velocity_to_entity_position()
 *   ├─> saved = DAT_00355044                   [Save current entity]
 *   ├─> FUN_0025c258(&selector)                [Eval entity selector]
 *   ├─> FUN_0025c258(&speed_int)               [Eval speed]
 *   ├─> FUN_0025c258(&angle_int)               [Eval angle]
 *   ├─> FUN_0025d6c0(selector, saved)          [Select entity]
 *   ├─> speed_scaled = (speed/norm) * 0.03125 * delta  [Scale speed]
 *   ├─> angle_fixed = FUN_00216690(angle/norm) [Float → fixed-point]
 *   ├─> cos_val = FUN_00305130(angle_fixed)    [Cosine for X]
 *   ├─> entity[+0x30] += speed * cos           [Update X position]
 *   ├─> sin_val = FUN_00305218(angle_fixed)    [Sine for Z]
 *   └─> entity[+0x34] += speed * sin           [Update Z position]
 *
 * Velocity Calculation:
 *
 * speed_scaled = (speed / DAT_00352bc8) * 0.03125 * DAT_003555bc
 *
 * Components:
 * - speed / DAT_00352bc8: Normalize speed (likely DAT_00352bc8 = 100.0 or similar)
 * - * 0.03125: Unit conversion (1/32, game units → world units)
 * - * DAT_003555bc: Frame delta (ensures constant speed at any frame rate)
 *
 * Angle Conversion:
 * - angle / DAT_00352bc8: Normalize angle (degrees or custom units)
 * - FUN_00216690: Convert to fixed-point (likely 16.16 or similar)
 * - Fixed-point used for fast trig lookup tables
 *
 * Polar to Cartesian:
 * dx = speed * cos(angle)  [X component, horizontal left/right]
 * dz = speed * sin(angle)  [Z component, depth forward/back]
 *
 * Typical Script Sequences:
 * ```
 * # Sequence 1: Move entity forward at constant speed
 * 0x5D [entity_idx] [speed] 0       # angle=0: move in +X direction
 *
 * # Sequence 2: Circular motion (loop)
 * 0x36 [angle_var]                  # read current angle
 * 0x5D [entity_idx] 50 [angle_var]  # move at angle
 * 0x1E [angle_var] 10               # increment angle by 10
 * 0x0E :circular_loop               # goto loop
 *
 * # Sequence 3: Chase player
 * 0x70 [player]                     # calculate angle to player
 * 0x37 [angle_var]                  # store angle
 * 0x5D [enemy] 30 [angle_var]       # move toward player
 *
 * # Sequence 4: Zigzag pattern
 * 0x5D [entity] 20 45               # move at 45°
 * 0x33 # sync (wait frames)
 * 0x5D [entity] 20 315              # move at 315° (-45°)
 * 0x33 # sync
 * 0x0E :zigzag_loop                 # repeat
 * ```
 *
 * Speed Scaling Analysis:
 * - If DAT_00352bc8 = 100.0:
 *   - speed=100 → normalized=1.0
 *   - speed=50 → normalized=0.5 (half speed)
 * - 0.03125 factor:
 *   - Likely converts from scripting units to world meters
 *   - Could be related to tile size (32 units per tile?)
 * - DAT_003555bc (frame delta):
 *   - Typical value: 1-2 at 60 FPS
 *   - Ensures consistent speed regardless of frame rate
 *   - Higher values = faster movement (more ticks per frame)
 *
 * Angle Conventions:
 * - Likely 0-360° or 0-4096 units (full circle)
 * - 0°: +X axis (right)
 * - 90°: +Z axis (forward/up in top-down)
 * - 180°: -X axis (left)
 * - 270°: -Z axis (back/down in top-down)
 * - Clockwise or counter-clockwise depends on coordinate system
 *
 * Entity Position Fields:
 * +0x20-0x2B: Full position (12 bytes = 3 floats XYZ)
 * +0x20: Position Y (vertical, not modified here)
 * +0x24: Position X (horizontal, modified as +0x30 with different base?)
 * +0x28: Position Z (depth, modified as +0x34 with different base?)
 *
 * Note: Offset discrepancy (+0x20-0x2B vs +0x30/0x34):
 * - Original decompile shows +0x30/0x34 for position
 * - Other opcodes (0x53, 0x54) use +0x20/24/28
 * - Likely: +0x30 = base+0x20+0x10 (different view of same struct)
 * - Or: velocity accumulator vs actual position?
 *
 * Performance Notes:
 * - Trig functions optimized (hardware or lookup table)
 * - Fixed-point conversion reduces FPU load
 * - Direct position update (no collision check here)
 * - Called per-frame for moving entities
 *
 * Use Cases:
 * - AI character movement (patrol, chase, flee)
 * - Projectile trajectories (bullets, arrows, magic)
 * - Particle system motion (explosion debris)
 * - Scripted cutscene movement (character walks to point)
 * - Camera dolly effects (orbit, pan)
 *
 * Comparison with Related Opcodes:
 * - 0x54/0x55: Set absolute entity position (teleport)
 * - 0x5D: Add relative velocity (smooth movement)
 * - 0x70/0x71: Calculate angle/distance (query, don't modify)
 * - 0x75: Calculate angle between entities (no movement)
 *
 * Collision Handling:
 * - This opcode does NOT check collision
 * - Entity can move through walls/obstacles
 * - Game logic elsewhere handles collision response
 * - Typical pattern: move → check collision → adjust position
 *
 * Frame-Rate Independence:
 * - DAT_003555bc compensates for variable frame times
 * - Ensures consistent movement speed at 30/60/120 FPS
 * - Formula: distance_per_frame = speed * delta_time
 * - Critical for gameplay fairness
 *
 * Coordinate System Notes:
 * - Y-up coordinate system (Y = vertical height)
 * - XZ plane = horizontal ground
 * - This opcode only modifies XZ (horizontal movement)
 * - Gravity/jumping handled by other systems
 *
 * Fixed-Point Angle Format:
 * - FUN_00216690 converts float → fixed-point
 * - Likely 16.16 fixed-point (65536 = full circle)
 * - Or 12.12 (4096 = full circle, common in PS2)
 * - Fixed-point enables fast lookup table trig
 *
 * Trig Function Implementation:
 * - FUN_00305130/00305218: Likely MIPS cop1 (FPU) or VU0 ops
 * - Could be lookup table with interpolation
 * - Returns double, cast to float (precision preserved)
 * - Accuracy: ~0.01° typical for fixed-point trig
 *
 * Movement Patterns:
 * - Straight line: angle constant, speed constant
 * - Circular: angle increments, speed constant
 * - Spiral: angle increments, speed increases
 * - Wave: angle oscillates, speed constant
 * - Orbit: angle based on center point, speed constant
 *
 * Error Handling:
 * - No validation on speed or angle ranges
 * - Negative speed: moves in opposite direction (valid)
 * - Out-of-range angle: wraps via trig functions (valid)
 * - Entity selection failure: modifies wrong memory (crash risk)
 *
 * Cross-References:
 * - analyzed/ops/0x54_0x55_set_entity_position.c: Absolute position setters
 * - analyzed/ops/0x70_submit_angle_to_target.c: Angle calculation
 * - analyzed/ops/0x71_submit_distance_to_target.c: Distance calculation
 * - analyzed/ops/0x75_calculate_entity_distance_or_angle.c: Entity-to-entity
 * - analyzed/select_current_object_frame.c: Entity selection
 * - src/FUN_00216690.c: Float to fixed-point conversion
 *
 * Additional Notes:
 * - Movement is instantaneous (no acceleration/deceleration)
 * - For smooth acceleration, call multiple times with increasing speed
 * - Typical AI: calculate angle to target, move with 0x5D
 * - Camera could use this for smooth orbits (rare, usually hardcoded)
 * - Particles: spawn with random angles for explosion spread
 */
