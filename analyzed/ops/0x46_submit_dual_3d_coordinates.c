// Opcode 0x46 — submit_dual_3d_coordinates (analyzed)
// Original: FUN_0025dff0
//
// Summary:
// - Allocates 8 floats (2 x 3D coordinate triplets) on stack (DAT_70000000, 0x20 bytes = 8 words).
// - Evaluates 6 VM expressions (2 triplets: xyz0, xyz1) via bytecode interpreter.
// - Normalizes each value by dividing by fGpffff8c04 (global scale factor).
// - Stores normalized floats into stack-allocated buffer (2 iterations, 3 values each = 6 floats).
// - Calls FUN_00217d70(x0,y0,z0, x1,y1,z1) to submit both coordinate triplets to graphics pipeline.
// - Pops stack allocation (restores DAT_70000000).
// - Returns 0.
//
// Typical usage:
//   0x46 <x0_expr> <y0_expr> <z0_expr> <x1_expr> <y1_expr> <z1_expr>  # Submit two 3D points
//
// Context:
// - Part of 3D graphics submission system alongside 0x43 (vertex streams) and 0x45 (single point).
// - FUN_00217d70 is a low-level graphics primitive submitter that:
//   - Copies coordinates to staging globals (DAT_0058be90/94/98 for second triplet,
//     DAT_0058c0a8/ac/b0 for first triplet)
//   - Backs up current render state to reference globals (DAT_0055f8d8/dc/e0/e4/e8)
//   - Sets rendering flags (uGpffffb6e1=0x23, cGpffffad2f=0xff, uGpffffad30=1)
//   - Configures primitive type and mode (uGpffffad2c=0x20, uGpffffad2d=2, uGpffffad2e=0)
//   - Clears rendering counters (uGpffffbb14=0, uGpffffbb0c=0)
// - Common pattern: submit start/end points for line rendering, bounding box corners, or
//   camera view frustum corners.
//
// PS2 notes:
// - Stack pointer DAT_70000000 is PS2 scratchpad RAM pointer (0x70000000 base, 16KB fast memory).
// - Overflow check (0x70003fff limit) ensures stack doesn't exceed scratchpad bounds.
// - FUN_0026bf90(0) is stack overflow handler (likely fatal error/panic).
// - fGpffff8c04 is rendering scale constant (converts script integer coords to float world space).
// - Loop structure evaluates 6 expressions in nested pattern: outer loop = 2 triplets,
//   inner loop = 3 coords per triplet (matches iVar5 countdown from 2 to 0).
//
// Keep unresolved externs by their original labels for traceability.

#include <stdint.h>

typedef unsigned int uint;
typedef unsigned long long uint64_t;

// VM entry (analyzed name)
extern void bytecode_interpreter(void *result_out); // orig FUN_0025c258

// Graphics dual-coordinate submitter (sets up render state for 2-point primitive)
extern void FUN_00217d70(float x0, float y0, float z0, float x1, float y1, float z1);

// Stack overflow handler (fatal error when scratchpad RAM exceeded)
extern void FUN_0026bf90(int error_code);

// Globals
extern uint *DAT_70000000; // PS2 scratchpad RAM stack pointer (0x70000000 base)
extern float fGpffff8c04;  // Coordinate normalization scale (script→world space conversion)

// Globals affected by FUN_00217d70 (referenced for documentation):
// - DAT_0058c0a8/ac/b0: first coordinate triplet (x0,y0,z0) staging area
// - DAT_0058be90/94/98: second coordinate triplet (x1,y1,z1) staging area
// - DAT_0055f8d8/dc/e0/e4/e8: backup of current render state
// - uGpffffb6e1: render state flag (set to 0x23)
// - cGpffffad2f: render enable flag (set to 0xff)
// - uGpffffad30: render active counter (set to 1)
// - uGpffffad2c/2d/2e: primitive type/mode configuration (0x20, 2, 0)
// - uGpffffbb14/bb0c: rendering loop counters (cleared)

// Original signature: undefined8 FUN_0025dff0(void)
uint64_t opcode_0x46_submit_dual_3d_coordinates(void)
{
  uint *stack_ptr;
  float scale;
  int iVar3;         // outer loop index (triplet counter)
  int iVar5;         // inner loop countdown (coord countdown)
  int iVar6;         // outer loop counter (triplet counter)
  float *pfVar4;     // destination pointer for normalized floats
  int aiStack_80[4]; // VM evaluator output buffer

  // Allocate 8 words (0x20 bytes) on scratchpad stack for 2 triplets (6 floats used, 2 padding)
  stack_ptr = DAT_70000000;
  DAT_70000000 = DAT_70000000 + 8;

  // Check for scratchpad stack overflow (0x70000000 + 0x4000 = 0x70004000 limit)
  if ((uint *)0x70003fff < DAT_70000000)
  {
    FUN_0026bf90(0); // Fatal: scratchpad RAM exceeded
  }

  scale = fGpffff8c04; // Load global coordinate scale
  iVar6 = 0;           // Triplet index (0, 1)
  iVar3 = 0;           // Byte offset accumulator

  // Outer loop: process 2 coordinate triplets
  do
  {
    iVar6 = iVar6 + 1;
    iVar5 = 2;                                  // Inner countdown: 2, 1, 0 (3 coords)
    pfVar4 = (float *)(iVar3 + (int)stack_ptr); // Destination for this triplet

    // Inner loop: evaluate and normalize 3 coordinates (x, y, z)
    do
    {
      iVar5 = iVar5 + -1;
      bytecode_interpreter(aiStack_80);       // Evaluate next coordinate expression
      *pfVar4 = (float)aiStack_80[0] / scale; // Normalize and store
      pfVar4 = pfVar4 + 1;                    // Advance to next float slot
    } while (-1 < iVar5);

    iVar3 = iVar6 * 0xc; // Advance byte offset by 12 (3 floats × 4 bytes)
  } while (iVar6 < 2);

  // Submit both coordinate triplets to graphics pipeline
  // stack_ptr[0..2] = first triplet (x0,y0,z0)
  // stack_ptr[3..5] = second triplet (x1,y1,z1)
  FUN_00217d70(stack_ptr[0], stack_ptr[1], stack_ptr[2],
               stack_ptr[3], stack_ptr[4], stack_ptr[5]);

  // Pop stack allocation (restore scratchpad pointer)
  DAT_70000000 = DAT_70000000 + -8;

  return 0;
}

/*
 * Cross-references (globals.json):
 * - FUN_0025dff0 reads DAT_70000000 (0x0025e01c, 0x0025e0b8)
 * - FUN_0025dff0 writes DAT_70000000 (0x0025e02c, 0x0025e0dc)
 * - Reads fGpffff8c04 (0x00352c04) for coordinate normalization
 *
 * Usage notes:
 * - Likely used for rendering lines, bounding boxes, or defining view frustum corners
 * - Common in cutscene camera setup or environmental effect boundaries
 * - Typical coordinate values are integer script values (e.g., 1000-10000 range) normalized
 *   to world space floats by dividing by scale factor
 *
 * Example bytecode sequence:
 *   0x46 [expr:start_x] [expr:start_y] [expr:start_z]
 *        [expr:end_x] [expr:end_y] [expr:end_z]
 *   → Renders primitive from (start_x/scale, start_y/scale, start_z/scale)
 *     to (end_x/scale, end_y/scale, end_z/scale)
 *
 * TODO:
 * - Find example scripts using 0x46 to understand typical coordinate patterns
 * - Analyze FUN_00217d70 callers to determine what geometric primitives this supports
 *   (lines, quads, bounding volumes?)
 * - Determine relationship between 0x45, 0x46, and 0x43 in rendering sequences
 */
