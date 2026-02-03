// Opcode 0x47 — submit_single_3d_coordinate (analyzed)
// Original: FUN_0025e0e8
//
// Summary:
// - Evaluates 3 VM expressions (single XYZ coordinate triplet).
// - Normalizes each value by dividing by DAT_00352b78 (global scale factor).
// - Calls FUN_00217d40(x, y, z) to conditionally update graphics state.
// - Returns 0.
//
// Typical usage:
//   0x47 <x_expr> <y_expr> <z_expr>  # Submit single 3D coordinate
//
// Context:
// - Companion to opcode 0x46 (submit_dual_3d_coordinates) in graphics pipeline.
// - FUN_00217d40 only acts when cGpffffb6e1 == 0x23 (render state flag set by 0x46):
//   - Clears uGpffffbb14 (rendering counter)
//   - Copies coordinates to active rendering state (DAT_0058c0a8/ac/b0)
//   - Calls FUN_00217a70() to trigger geometry submission
// - When cGpffffb6e1 != 0x23, FUN_00217d40 is a no-op (early return).
// - Typical sequence: 0x46 (two endpoints) → 0x47 (additional point) for multi-point primitives.
//
// PS2 notes:
// - DAT_00352b78 is coordinate normalization scale (converts script ints to world floats).
// - cGpffffb6e1 == 0x23 indicates graphics pipeline is ready for additional coordinates.
// - FUN_00217a70 likely builds and submits VIF/DMA packets to PS2 GS.
// - This allows scripts to define complex primitives (lines, triangles, polygons) by
//   submitting multiple coordinate sets in sequence.
//
// Keep unresolved externs by their original labels for traceability.

#include <stdint.h>

typedef unsigned long long uint64_t;

// VM entry (analyzed name)
extern void bytecode_interpreter(void *result_out); // orig FUN_0025c258

// Graphics coordinate submitter (conditional on cGpffffb6e1 == 0x23)
// Sets DAT_0058c0a8/ac/b0 and calls FUN_00217a70() to trigger rendering
extern void FUN_00217d40(float x, float y, float z);

// Globals
extern float DAT_00352b78; // Coordinate normalization scale factor
extern char cGpffffb6e1;   // Render state flag (must be 0x23 for FUN_00217d40 to act)

// Globals affected by FUN_00217d40 (when cGpffffb6e1 == 0x23):
// - DAT_0058c0a8: X coordinate (float)
// - DAT_0058c0ac: Y coordinate (float)
// - DAT_0058c0b0: Z coordinate (float)
// - uGpffffbb14: rendering counter (cleared to 0)
// Then calls FUN_00217a70() which likely builds VIF/GS packets

// Original signature: undefined8 FUN_0025e0e8(void)
uint64_t opcode_0x47_submit_single_3d_coordinate(void)
{
  float scale;
  float *pfVar2;
  int iVar3;
  float afStack_70[4]; // Output buffer for normalized coordinates
  int aiStack_60[4];   // VM evaluator output buffer

  scale = DAT_00352b78; // Load normalization scale
  pfVar2 = afStack_70;  // Destination pointer
  iVar3 = 2;            // Countdown: 2, 1, 0 (3 coordinates)

  // Loop: evaluate 3 expressions and normalize
  do
  {
    iVar3 = iVar3 + -1;
    bytecode_interpreter(aiStack_60);       // Evaluate next coordinate expression
    *pfVar2 = (float)aiStack_60[0] / scale; // Normalize and store
    pfVar2 = pfVar2 + 1;                    // Advance to next float slot
  } while (-1 < iVar3);

  // Submit coordinate triplet to graphics pipeline (conditional on render state)
  FUN_00217d40(afStack_70[0], afStack_70[1], afStack_70[2]);

  return 0;
}

/*
 * Cross-references (globals.json):
 * - FUN_0025e0e8 reads DAT_00352b78 (0x0025e10c) for normalization scale
 *
 * Usage pattern:
 * Opcode 0x46 submits two coordinate triplets and sets cGpffffb6e1 = 0x23.
 * Opcode 0x47 then submits additional coordinates while that flag is active.
 * This allows constructing multi-vertex primitives:
 *
 *   0x46 <x0> <y0> <z0> <x1> <y1> <z1>  # Set up line/primitive endpoints, flag=0x23
 *   0x47 <x2> <y2> <z2>                  # Add third point (triangle)
 *   0x47 <x3> <y3> <z3>                  # Add fourth point (quad)
 *   ...
 *
 * When cGpffffb6e1 != 0x23, opcode 0x47 evaluates coordinates but FUN_00217d40
 * does nothing (early return), allowing scripts to conditionally skip geometry.
 *
 * Related opcodes:
 * - 0x45: submit_single_3d_point (calls FUN_00217e18 for pipeline initialization)
 * - 0x46: submit_dual_3d_coordinates (calls FUN_00217d70, sets up for 0x47)
 * - 0x43: build_and_submit_3way_vertex_streams (bulk vertex submission)
 *
 * TODO:
 * - Analyze FUN_00217a70 to understand actual VIF/GS packet construction
 * - Find example scripts using 0x46+0x47 sequences to determine typical primitives
 * - Document relationship between cGpffffb6e1 states and rendering pipeline stages
 * - Determine what geometric primitives are supported (lines, triangles, quads, polygons)
 */
