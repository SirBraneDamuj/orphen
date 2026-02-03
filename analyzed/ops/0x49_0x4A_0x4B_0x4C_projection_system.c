// analyzed/ops/0x49_0x4A_0x4B_0x4C_projection_system.c
// Original: FUN_0025e1f8 (0x49), FUN_0025e250 (0x4A), FUN_0025e420 (0x4B), FUN_0025e520 (0x4C)
// Opcodes: 0x49, 0x4A, 0x4B, 0x4C
// Handlers: 3D projection/ray-casting system for entity targeting

// Behavior Summary:
//
// 0x49 (FUN_0025e1f8): Submit entity position to projection system
// - Evaluates 1 expression (entity index).
// - If index < 0x100, selects entity from pool (DAT_0058beb0 + index * 0xEC).
// - Reads entity position from pool (+0x10/14/18 offsets = +0xED0/ED4/ED8 from base).
// - Calls FUN_00217d10 to set camera/target position.
// - Returns 0.
//
// 0x4A (FUN_0025e250): Initialize projection parameters
// - Reads immediate byte from stream (flag/mode).
// - Evaluates 6 expressions (4 position/direction params, 2 distance params).
// - Normalizes all by DAT_00352b80 (world scale).
// - Stores normalized values in globals (DAT_005721b8/bc/c0 = direction plane).
// - Stores camera reference position (DAT_00355d00/04/08 = DAT_0058be80/84/88).
// - Stores distance parameters (DAT_00355d0c/10/14 = near/far/extent).
// - Clears flags (DAT_00355d18/1c = 0).
// - Calls FUN_00218ea0 (sets uGpffffad31=0, clearing busy flag).
// - If immediate byte != 0, initializes graphics pipeline and calls FUN_00217d70.
// - Returns 0.
//
// 0x4B (FUN_0025e420): Query projection intersection (ray-cast test)
// - No parameters evaluated.
// - Computes delta from stored position (DAT_00355d00/04) to listener (DAT_0058c0a8/ac).
// - Calls FUN_00216608 (stub function, likely distance/normalize).
// - Updates stored position to current listener position.
// - Computes angle via FUN_00305408 (atan2).
// - Computes target position using cos/sin of angle and DAT_00352b88 distance.
// - Calls FUN_002189b0 (ray-cast/intersection test) with:
//   - Projection plane (DAT_005721b8/bc/c0)
//   - Computed target position
//   - Listener position (DAT_0058c0a8/ac/b0)
//   - Distance parameters (DAT_00355d0c/10)
// - Returns true if distance < DAT_00355d10 (intersection found).
//
// 0x4C (FUN_0025e520): Set projection distance parameter
// - Evaluates 1 expression (distance value).
// - Normalizes by DAT_00352b8c.
// - Stores in DAT_0035564c (global distance/scale parameter).
// - Returns 0.

// Usage Pattern:
// These opcodes implement a 3D projection/ray-casting system, likely for:
// - Enemy targeting (finding entities in view cone)
// - Camera frustum culling (testing if entities visible)
// - Line-of-sight checks (ray-casting from camera to entity)
//
// Typical sequence:
// 1. 0x4A [params...] # Initialize projection plane and distances
// 2. 0x49 [entity_idx] # Test if entity in view
// 3. 0x4B             # Query intersection result (true/false)
// 4. 0x4C [distance]  # Update projection distance

// Related:
// - DAT_005721b8/bc/c0: Projection plane direction (normalized XYZ vector)
// - DAT_00355d00/04/08: Camera reference position
// - DAT_00355d0c/10/14: Near/far distances and extent for projection
// - DAT_0058c0a8/ac/b0: Listener/camera position (current frame)
// - DAT_0035564c: Global distance/scale parameter (used by interpolation)
// - FUN_002189b0: Ray-cast/intersection test (returns distance)
// - FUN_00216608: Distance/normalize stub (optimized out?)
// - FUN_00217d10: Set camera/target position

// PS2 Architecture:
// - Projection system uses normalized direction vectors (unit sphere).
// - Ray-casting for entity visibility/targeting (common in PS2 games).
// - Distance parameters define view frustum (near/far planes).
// - Listener position updated per-frame for camera tracking.
// - Intersection test returns distance (< threshold = visible).

#include <stdint.h>
#include <stdbool.h>

// External declarations
typedef void (*bytecode_evaluator_t)(void *);
extern bytecode_evaluator_t FUN_0025c258; // Bytecode expression evaluator

// Graphics system functions
extern void FUN_00217d10(float x, float y, float z);                                  // Set camera/target position
extern void FUN_00217d70(float x1, float y1, float z1, float x2, float y2, float z2); // Dual coord submit
extern void FUN_00217e18(int param);                                                  // Graphics pipeline init
extern void FUN_00218ea0(void);                                                       // Clear busy flag (uGpffffad31=0)

// Math functions
extern void FUN_00216608(float x, float y);     // Distance/normalize (stub)
extern uint32_t FUN_00305408(float y, float x); // atan2
extern float FUN_00305130(uint32_t angle);      // cos
extern float FUN_00305218(uint32_t angle);      // sin
extern float FUN_002189b0(float plane_x, float plane_y, float plane_z,
                          float target_x, float target_y, float target_z,
                          float near_dist, float far_dist); // Ray-cast test

// Current selected entity pointer
extern void *DAT_00355044;

// Entity pool base
extern uint8_t DAT_0058beb0;

// Bytecode stream pointer
extern uint8_t *DAT_00355cd0;

// Projection plane direction (normalized)
extern float DAT_005721b8; // Plane X
extern float DAT_005721bc; // Plane Y
extern float DAT_005721c0; // Plane Z

// Camera reference position (stored for delta computation)
extern float DAT_00355d00; // Reference X
extern float DAT_00355d04; // Reference Y
extern float DAT_00355d08; // Reference Z (computed from DAT_0058be88 - DAT_00352b84)

// Distance parameters
extern float DAT_00355d0c; // Near distance
extern float DAT_00355d10; // Far distance
extern float DAT_00355d14; // Extent/width

// Projection flags
extern uint32_t DAT_00355d18;
extern uint32_t DAT_00355d1c;

// Listener/camera position (current frame)
extern float DAT_0058c0a8; // Listener X
extern float DAT_0058c0ac; // Listener Y
extern float DAT_0058c0b0; // Listener Z

// Camera position globals
extern float DAT_0058be80; // Camera X
extern float DAT_0058be84; // Camera Y
extern float DAT_0058be88; // Camera Z

// Entity alternate positions (used for projection)
extern float DAT_0058bed0; // Entity alt X
extern float DAT_0058bed4; // Entity alt Y
extern float DAT_0058bed8; // Entity alt Z

// Graphics state
extern uint32_t DAT_0058bf0c; // Rotation/angle
extern float DAT_0058befc;    // Offset value
extern float DAT_0058bf08;    // Height offset

// Additional globals
extern float DAT_00354c94; // Angle parameter
extern float DAT_00354c98; // Scale/distance parameter

// Normalization scales
extern float DAT_00352b80; // General world scale
extern float DAT_00352b84; // Height offset scale
extern float DAT_00352b88; // Distance scale (used in 0x4B)
extern float DAT_00352b8c; // Distance scale (used in 0x4C)

// Global distance/scale parameter
extern float DAT_0035564c;

// Busy flag
extern uint8_t uGpffffad31;

// Opcode 0x49: Submit entity position to projection system
uint64_t opcode_0x49_submit_entity_position_to_projection(void)
{
  uint32_t index[4];

  // Evaluate entity index expression
  FUN_0025c258(index);

  if (index[0] < 0x100)
  {
    // Select entity from pool
    DAT_00355044 = (void *)(&DAT_0058beb0 + index[0] * 0xEC);

    // Read entity position from pool (offsets +0x10/14/18 from entity base)
    // Note: DAT_0058bed0 is base+0x10, so index*0x76 = (index*0xEC)/2
    float entity_x = *((float *)(&DAT_0058bed0 + index[0] * 0x76));
    float entity_y = *((float *)(&DAT_0058bed4 + index[0] * 0x76));
    float entity_z = *((float *)(&DAT_0058bed8 + index[0] * 0x76));

    // Submit entity position to camera/target system
    FUN_00217d10(entity_x, entity_y, entity_z);
  }

  return 0;
}

// Opcode 0x4A: Initialize projection parameters
uint64_t opcode_0x4a_init_projection_parameters(void)
{
  uint8_t mode_flag;
  int32_t param0, param1, param2, param3, param4, param5;

  // Read immediate byte (mode/flag)
  mode_flag = *DAT_00355cd0;
  DAT_00355cd0 = DAT_00355cd0 + 1;

  // Evaluate 6 parameters
  FUN_0025c258(&param0);
  FUN_0025c258(&param1);
  FUN_0025c258(&param2);
  FUN_0025c258(&param3);
  FUN_0025c258(&param4);
  FUN_0025c258(&param5);

  // Normalize and store projection plane direction
  DAT_005721b8 = (float)param0 / DAT_00352b80; // Plane X
  DAT_005721bc = (float)param1 / DAT_00352b80; // Plane Y
  DAT_005721c0 = (float)param2 / DAT_00352b80; // Plane Z

  // Store additional projection parameters
  DAT_00355d14 = (float)param5 / DAT_00352b80; // Extent/width
  DAT_00355d0c = (float)param3 / DAT_00352b80; // Near distance
  DAT_00355d10 = (float)param4 / DAT_00352b80; // Far distance

  // Store camera reference position (for delta computation)
  DAT_00355d08 = DAT_0058be88 - DAT_00352b84; // Reference Z
  DAT_00355d00 = DAT_0058be80;                // Reference X
  DAT_00355d04 = DAT_0058be84;                // Reference Y

  // Clear projection flags
  DAT_00355d18 = 0;
  DAT_00355d1c = 0;

  // Clear busy flag
  FUN_00218ea0(); // Sets uGpffffad31 = 0

  // If mode flag set, initialize graphics pipeline
  if (mode_flag != 0)
  {
    uint32_t rotation = DAT_0058bf0c;
    float scale = DAT_00354c98;

    FUN_00217e18(0);

    // Compute adjusted position using trig functions
    float cos_rot = FUN_00305130(rotation);
    float sin_rot = FUN_00305218(rotation);
    float sin_angle = FUN_00305218(DAT_00354c94);

    float adj_x = DAT_0058bed0 - scale * cos_rot;
    float adj_y = DAT_0058bed4 - scale * sin_rot;
    float adj_z = DAT_0058bed8 + scale * sin_angle;

    // Submit dual coordinates
    FUN_00217d70(adj_x, adj_y, adj_z,
                 DAT_0058bed0, DAT_0058bed4,
                 DAT_0058befc + DAT_0058bf08);
  }

  return 0;
}

// Opcode 0x4B: Query projection intersection (ray-cast test)
bool opcode_0x4b_query_projection_intersection(void)
{
  float distance_scale;
  float delta_x, delta_y;
  uint32_t angle;
  float cos_angle, sin_angle;
  float target_x, target_y;
  float intersection_distance;

  // Load distance scale
  distance_scale = DAT_00352b88;

  // Compute delta from stored position to current listener position
  delta_x = DAT_0058c0a8 - DAT_00355d00;
  delta_y = DAT_0058c0ac - DAT_00355d04;

  // Normalize/compute distance (stub function)
  FUN_00216608(delta_x, delta_y);

  // Update stored position to current listener position
  DAT_00355d00 = DAT_0058c0a8;
  DAT_00355d04 = DAT_0058c0ac;

  // Compute angle to target
  angle = FUN_00305408(delta_y, delta_x); // atan2

  // Compute target position using angle and distance
  cos_angle = FUN_00305130(angle);
  sin_angle = FUN_00305218(angle);
  target_x = DAT_0058c0a8 + cos_angle * distance_scale;
  target_y = DAT_0058c0ac + sin_angle * distance_scale;

  // Perform ray-cast intersection test
  intersection_distance = FUN_002189b0(
      DAT_005721b8, DAT_005721bc, DAT_005721c0, // Projection plane
      target_x, target_y, DAT_0058c0b0,         // Target position
      DAT_00355d0c, DAT_00355d10                // Near/far distances
  );

  // Return true if intersection within far distance
  return intersection_distance < DAT_00355d10;
}

// Opcode 0x4C: Set projection distance parameter
uint64_t opcode_0x4c_set_projection_distance(void)
{
  int32_t distance[4];

  // Evaluate distance expression
  FUN_0025c258(distance);

  // Normalize and store in global distance parameter
  DAT_0035564c = (float)distance[0] / DAT_00352b8c;

  return 0;
}

/*
 * Function Call Hierarchy:
 *
 * opcode_0x49_submit_entity_position_to_projection()
 *   ├─> FUN_0025c258(&index)              [Eval entity index]
 *   └─> if (index < 0x100)
 *         ├─> DAT_00355044 = pool + idx*0xEC  [Select entity]
 *         └─> FUN_00217d10(x, y, z)           [Submit position]
 *
 * opcode_0x4a_init_projection_parameters()
 *   ├─> Read immediate byte (mode)
 *   ├─> FUN_0025c258(...) × 6             [Eval 6 parameters]
 *   ├─> Store normalized values in globals
 *   ├─> FUN_00218ea0()                    [Clear busy flag]
 *   └─> if (mode != 0)
 *         ├─> FUN_00217e18(0)             [Init graphics pipeline]
 *         ├─> FUN_00305130/00305218       [Trig: cos/sin]
 *         └─> FUN_00217d70(...)           [Submit dual coords]
 *
 * opcode_0x4b_query_projection_intersection()
 *   ├─> Compute delta_x/y from stored to current
 *   ├─> FUN_00216608(delta_x, delta_y)   [Normalize - stub]
 *   ├─> FUN_00305408(delta_y, delta_x)   [atan2 angle]
 *   ├─> FUN_00305130/00305218(angle)     [cos/sin]
 *   ├─> Compute target position
 *   ├─> FUN_002189b0(plane, target, listener, distances)  [Ray-cast]
 *   └─> return distance < far_distance
 *
 * opcode_0x4c_set_projection_distance()
 *   ├─> FUN_0025c258(&distance)           [Eval distance]
 *   └─> DAT_0035564c = distance / scale   [Store normalized]
 *
 * Projection System Flow:
 * 1. Initialize projection plane and distances (0x4A)
 *    - Sets up view frustum (near/far planes)
 *    - Defines projection direction (normalized vector)
 *    - Stores camera reference position
 * 2. Submit entity position for testing (0x49)
 *    - Reads entity XYZ from pool
 *    - Passes to camera/target system
 * 3. Query intersection result (0x4B)
 *    - Computes angle from camera to target
 *    - Projects ray in that direction
 *    - Tests intersection with projection plane
 *    - Returns true if within frustum
 * 4. Update distance parameter (0x4C)
 *    - Adjusts global distance/scale
 *    - Used by interpolation system
 *
 * Ray-Cast Test (FUN_002189b0):
 * - Takes projection plane (normalized direction vector)
 * - Takes target position (computed from angle + distance)
 * - Takes listener position (camera)
 * - Takes near/far distance parameters
 * - Returns distance to intersection (or large value if no intersection)
 * - Used for visibility testing and targeting
 *
 * Coordinate Systems:
 * - Entity pool positions: Offset +0x10/14/18 from entity base (0xEC stride)
 * - Camera reference: DAT_00355d00/04/08 (stored for delta computation)
 * - Listener position: DAT_0058c0a8/ac/b0 (updated per-frame)
 * - Projection plane: DAT_005721b8/bc/c0 (normalized direction)
 * - Distance parameters: DAT_00355d0c/10/14 (near/far/extent)
 *
 * Usage Patterns:
 * ```
 * # Setup targeting system
 * 0x4A 0x01 [plane_x] [plane_y] [plane_z] [near] [far] [extent]
 *
 * # Test if entity 5 is in view
 * 0x49 0x05        # Submit entity 5 position
 * 0x4B             # Query intersection (returns true/false)
 * 0x0D [offset]    # Branch if visible
 *
 * # Update projection distance
 * 0x4C 0x64        # Set distance to 100 (normalized)
 * ```
 *
 * Typical Script Sequences:
 * - Combat targeting: 0x4A → 0x49 (loop over enemies) → 0x4B → select closest
 * - Camera culling: 0x4A → 0x49 (loop over entities) → 0x4B → hide if false
 * - Line-of-sight: 0x4A → 0x49 (player/NPC) → 0x4B → trigger if visible
 *
 * Performance Notes:
 * - FUN_00216608 is stub (optimized out by compiler)
 * - Ray-cast test (FUN_002189b0) is complex (295 lines)
 * - Projection plane stored globally (avoid recomputation)
 * - Distance test uses simple < comparison (fast early-out)
 *
 * Related Opcodes:
 * - 0x47/0x48: submit_single_3d_coordinate (set camera position/target)
 * - 0x46: submit_dual_3d_coordinates (dual XYZ triplets)
 * - 0x45: submit_single_3d_point (graphics pipeline init)
 * - 0x70: submit_angle_to_target (angle computation)
 * - 0x71: submit_distance_to_target (distance computation)
 *
 * Cross-References:
 * - analyzed/ops/0x47_submit_single_3d_coordinate.c: Camera position setup
 * - analyzed/ops/0x48_submit_triple_3d_coordinate.c: Camera target setup
 * - analyzed/ops/0x70_submit_angle_to_target.c: Angle computation
 * - analyzed/ops/0x71_submit_distance_to_target.c: Distance computation
 * - src/FUN_002189b0.c: Ray-cast intersection test (295 lines)
 */
