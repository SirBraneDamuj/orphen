// analyzed/ops/0x48_submit_triple_3d_coordinate.c
// Original: FUN_0025e170
// Opcode: 0x48
// Handler: Submit 3D coordinate to camera/target system

// Behavior:
// - Evaluates 3 expressions (X, Y, Z coordinates).
// - Normalizes each by DAT_00352b7c (world→camera scale).
// - Calls FUN_00217d10 to apply coordinates (camera target/position).
// - FUN_00217d10 only acts if cGpffffb6e1 == 0x23 (graphics mode flag).
// - Returns 0 (standard opcode completion).

// Comparison with Related Opcodes:
// - 0x45: submit_single_3d_point → FUN_00217e18 (pipeline init)
// - 0x46: submit_dual_3d_coordinates → FUN_00217d70 (dual XYZ triplets)
// - 0x47: submit_single_3d_coordinate → FUN_00217d40 (conditional on cGpffffb6e1==0x23)
// - 0x48: submit_triple_3d_coordinate → FUN_00217d10 (conditional on cGpffffb6e1==0x23)
//
// Pattern suggests 0x47 and 0x48 are paired handlers for dual-coordinate systems
// (e.g., camera position vs target, or primary/secondary rendering contexts).

// Related:
// - DAT_00352b7c: Coordinate normalization scale (world units to camera space)
// - cGpffffb6e1: Graphics rendering mode flag (0x23 = active rendering context)
// - FUN_00217d10: Camera/target coordinate setter (sets DAT_0058be90/94/98, calls FUN_00217a70)
// - FUN_00217d40: Alternate coordinate setter (similar pattern, different target)
// - DAT_0058be90/94/98: Camera or target position (X/Y/Z) in world space

// PS2 Architecture:
// - Loop unrolled to evaluate 3 expressions (standard compiler optimization).
// - Normalization separates script space from rendering space.
// - Guard flag (cGpffffb6e1) prevents writes when rendering context inactive.
// - Pattern common in scene/camera management (separate position/target updates).

#include <stdint.h>

// External declarations
typedef void (*bytecode_evaluator_t)(void *);
extern bytecode_evaluator_t FUN_0025c258; // Bytecode expression evaluator

// Graphics system function - sets camera/target position
extern void FUN_00217d10(float x, float y, float z);

// Coordinate normalization scale
extern float DAT_00352b7c;

// Graphics rendering mode flag (0x23 = active context)
extern char cGpffffb6e1;

// Camera/target position globals (written by FUN_00217d10)
extern float DAT_0058be90; // X coordinate
extern float DAT_0058be94; // Y coordinate
extern float DAT_0058be98; // Z coordinate

// Opcode 0x48: Submit 3D coordinate triplet to camera/target system
uint64_t opcode_0x48_submit_triple_3d_coordinate(void)
{
  float normalized_coords[4]; // Stack buffer for normalized values
  int32_t raw_values[4];      // Stack buffer for evaluated expressions
  float scale;
  float *coord_ptr;
  int32_t loop_count;

  // Load normalization scale
  scale = DAT_00352b7c;
  coord_ptr = normalized_coords;

  // Evaluate and normalize 3 coordinate expressions
  // Original decompile shows unrolled loop (iVar3 = 2; do { ... iVar3--; } while (iVar3 > -1))
  loop_count = 2; // Loop runs 3 times (2, 1, 0)
  do
  {
    loop_count = loop_count - 1;

    // Evaluate next expression (X, Y, or Z)
    FUN_0025c258(raw_values);

    // Normalize: convert integer to float, divide by scale
    *coord_ptr = (float)raw_values[0] / scale;
    coord_ptr = coord_ptr + 1;
  } while (-1 < loop_count);

  // Submit normalized coordinates to camera/target system
  // FUN_00217d10 checks cGpffffb6e1==0x23 before writing DAT_0058be90/94/98
  FUN_00217d10(normalized_coords[0],  // X
               normalized_coords[1],  // Y
               normalized_coords[2]); // Z

  return 0;
}

/*
 * Function Call Hierarchy:
 *
 * opcode_0x48_submit_triple_3d_coordinate()
 *   ├─> FUN_0025c258(&raw_values[0])     [Eval expr 1: X coordinate]
 *   ├─> FUN_0025c258(&raw_values[0])     [Eval expr 2: Y coordinate]
 *   ├─> FUN_0025c258(&raw_values[0])     [Eval expr 3: Z coordinate]
 *   └─> FUN_00217d10(x, y, z)            [Submit to camera/target system]
 *         ├─> if (cGpffffb6e1 == 0x23)
 *         │     ├─> uGpffffbb0c = 0       [Clear busy flag]
 *         │     ├─> DAT_0058be90 = x      [Set X position]
 *         │     ├─> DAT_0058be94 = y      [Set Y position]
 *         │     ├─> DAT_0058be98 = z      [Set Z position]
 *         │     └─> FUN_00217a70()        [Apply position update]
 *         └─> return
 *
 * Normalization:
 * - Input: Integer values from script expressions
 * - Scale: DAT_00352b7c (world units to camera space conversion)
 * - Output: Floating-point coordinates in camera space
 * - Example: raw_value=1000, scale=10.0 → normalized=100.0
 *
 * Graphics Context Gate (cGpffffb6e1):
 * - Value 0x23: Active rendering context (writes allowed)
 * - Other values: Inactive context (FUN_00217d10 returns early)
 * - Used by opcodes 0x47, 0x48 to prevent updates during transitions
 * - Also checked by FUN_00217d40 (alternate coordinate setter)
 *
 * Camera/Target Position System:
 * - DAT_0058be90/94/98: Primary position storage (used by camera system)
 * - FUN_00217a70: Position application function (computes transformations)
 * - Separate from DAT_0058bed0/d4/d8 (alternate position set, used by some opcodes)
 * - Pattern suggests dual-coordinate system (position + target, or two views)
 *
 * Related Opcode Patterns:
 * - 0x45: FUN_00217e18 - Pipeline initialization, no guard flag
 * - 0x46: FUN_00217d70 - Dual triplet submission (6 coords)
 * - 0x47: FUN_00217d40 - Single triplet with guard (cGpffffb6e1==0x23)
 * - 0x48: FUN_00217d10 - Single triplet with guard (cGpffffb6e1==0x23)
 *
 * Usage Patterns:
 * - 0x47 + 0x48 likely paired for camera position + target
 * - Both check cGpffffb6e1 to synchronize updates
 * - FUN_00217d40 and FUN_00217d10 apply to different coordinate sets
 * - Typical sequence:
 *     0x47 [x1] [y1] [z1]  # Set camera position
 *     0x48 [x2] [y2] [z2]  # Set camera target
 *
 * Typical Script Sequences:
 * ```
 * # Set camera position and target
 * 0x47 0x64 0xC8 0x190   # Camera at (100, 200, 400) normalized
 * 0x48 0x00 0x00 0x00    # Look at origin (0, 0, 0)
 *
 * # Cutscene camera movement
 * 0x42                    # Start interpolation (advance_timed_interpolation)
 * 0x47 [x1] [y1] [z1]    # Camera position
 * 0x48 [x2] [y2] [z2]    # Camera target
 * 0x44                    # Advance interpolation
 * ```
 *
 * Coordinate System Notes:
 * - Script uses integer coordinates (typically hundreds to thousands)
 * - Normalization converts to floating-point world space
 * - DAT_00352b7c scale factor depends on map/scene scale
 * - Camera space uses float for smooth interpolation
 * - Z-axis typically represents depth/distance from camera
 *
 * Guard Flag States (cGpffffb6e1):
 * - 0x00: Idle/inactive rendering context
 * - 0x23: Active rendering context (camera updates allowed)
 * - Other values: Special modes (0x1f, 0x20, 0x21 seen in code)
 * - Set by graphics system during scene setup/transitions
 *
 * Performance Notes:
 * - Loop unrolled by compiler (typical PS2 EE optimization)
 * - Stack allocation for temp buffers (4-element float arrays)
 * - Single function call overhead (FUN_00217d10) per opcode
 * - Guard flag check prevents unnecessary matrix computations
 *
 * Cross-References:
 * - analyzed/ops/0x47_submit_single_3d_coordinate.c: Similar pattern, uses FUN_00217d40
 * - analyzed/ops/0x46_submit_dual_3d_coordinates.c: Dual triplet submission
 * - analyzed/ops/0x45_submit_single_3d_point.c: Pipeline initialization
 * - src/FUN_00217d10.c: Coordinate setter implementation
 * - src/FUN_00217a70.c: Position application (matrix computations)
 *
 * Related Globals:
 * - DAT_0058be90/94/98: Target position (this opcode's destination)
 * - DAT_0058bed0/d4/d8: Alternate position (used by 0x46, others)
 * - DAT_0058c0a8/ac/b0: Listener position (audio system)
 * - uGpffffbb0c: Busy flag (cleared by FUN_00217d10)
 * - cGpffffb6e1: Graphics mode flag (guard for updates)
 */
