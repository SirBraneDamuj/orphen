// analyzed/ops/0xB8_set_camera_distance.c
// Original: FUN_00263cb8
// Opcode: 0xB8
// Handler: Set camera distance/zoom parameters

// Behavior:
// - Evaluates one expression to get raw distance value (int).
// - Normalizes value by dividing by fGpffff8d44 scale factor.
// - Sets three camera-related globals:
//   - DAT_0032538c: Normalized distance (base value)
//   - fGpffffb6b8: Normalized distance (duplicate/alias)
//   - fGpffffb6bc: Near plane distance (base - 5.0)
// - Returns 0 (standard opcode completion).

// Related:
// - FUN_0023a860: Similar setter function (sets same 3 globals from float param).
// - Camera system uses these for view frustum/distance calculations.
// - fGpffffb6bc appears in many Japanese version rendering functions (near plane).
// - Typical values: base distance 10.0-50.0, near plane offset -5.0.

// PS2 Architecture:
// - Camera distance controls 3D projection/perspective.
// - Near plane (base - 5.0) defines closest visible Z coordinate.
// - Far plane likely controlled by separate global.

#include <stdint.h>

// External declarations
typedef void (*bytecode_evaluator_t)(void *);
extern bytecode_evaluator_t FUN_0025c258; // Bytecode expression evaluator
extern float fGpffff8d44;                 // Camera distance normalization scale factor

// Camera distance globals
extern float DAT_0032538c; // Base camera distance (normalized)
extern float fGpffffb6b8;  // Camera distance (duplicate/alias)
extern float fGpffffb6bc;  // Near plane distance (base - 5.0)

uint64_t opcode_0xb8_set_camera_distance(void)
{
  int32_t raw_distance;
  float normalized_distance;

  // Evaluate expression to get raw distance value
  FUN_0025c258(&raw_distance);

  // Normalize distance value
  normalized_distance = (float)raw_distance / fGpffff8d44;

  // Set camera distance parameters
  DAT_0032538c = normalized_distance;      // Base distance
  fGpffffb6b8 = normalized_distance;       // Distance alias (used by other systems)
  fGpffffb6bc = normalized_distance - 5.0; // Near plane offset

  return 0;
}

/*
 * Function Call Hierarchy:
 *
 * opcode_0xb8_set_camera_distance()
 *   ├─> FUN_0025c258(&raw_distance)       [Eval expr: distance value]
 *   └─> Sets three camera globals directly
 *         DAT_0032538c = normalized
 *         fGpffffb6b8 = normalized
 *         fGpffffb6bc = normalized - 5.0
 *
 * Camera Distance Parameters:
 * - DAT_0032538c: Primary camera distance storage (read by FUN_0022a418)
 * - fGpffffb6b8: Duplicate/alias (allows simultaneous access by different systems)
 * - fGpffffb6bc: Near clipping plane distance (defines closest visible Z)
 *
 * Near Plane Offset:
 * - Constant offset of -5.0 from base distance
 * - Prevents geometry clipping when too close to camera
 * - Japanese version rendering heavily uses fGpffffb6bc (20+ references)
 *
 * Related Function (FUN_0023a860):
 * - void FUN_0023a860(float param_1)
 * - Sets same 3 globals from float parameter
 * - Called by FUN_0027b380 with value 0x41700000 (15.0f)
 * - Suggests typical distance range: 10.0 - 50.0
 *
 * Normalization Scale:
 * - fGpffff8d44: Converts integer to float camera distance units
 * - Typical use: scripts pass integers (easier authoring), normalized to floats
 *
 * Usage Patterns:
 * - Camera positioning during cutscenes or area transitions
 * - Zoom effects (in/out by changing distance)
 * - Frustum configuration for 3D rendering
 * - Typical sequence: 0xB8 (set distance) -> render frame
 *
 * Related Opcodes:
 * - 0x45-0x47: submit_3d_coordinates (uses camera parameters for projection)
 * - 0x70: submit_angle_to_target (camera relative angles)
 * - 0x76: select_object_and_read_register (may read camera state)
 *
 * Example Script Usage:
 * ```
 * 0xB8 0x3200  # Set camera distance to 0x3200/scale (normalized ~25.0)
 * ```
 *
 * Camera System Context:
 * - Distance values control field of view (closer = wider FOV)
 * - Near plane prevents Z-fighting and clipping artifacts
 * - Duplicate globals (DAT_0032538c, fGpffffb6b8) allow:
 *   - One for game logic queries
 *   - One for rendering pipeline access
 *   - Prevents coupling between subsystems
 */
