// Opcode 0x45 — submit_single_3d_point (analyzed)
// Original: FUN_0025dfc8
//
// Summary:
// - Evaluates one VM expression (likely a 3D coordinate reference or index).
// - Calls FUN_00217e18 with the evaluated result to initiate graphics submission.
// - Returns 0.
//
// Typical usage:
//   0x45 <coordinate_expr>  # Submit single 3D point/coordinate to graphics pipeline
//
// Context:
// - Part of 3D graphics submission system alongside opcode 0x43 (vertex stream builder)
//   and 0x46 (dual coordinate submission).
// - FUN_00217e18 is a graphics pipeline initiator that sets up rendering state based on its parameter.
// - When param != 0, copies reference coordinates (DAT_0055f8d8/dc/e0) to active rendering
//   state (DAT_0058c0a8/ac/b0) and sets flags.
// - When param == 0, sets DAT_0058c0ea = 0x1e (likely a rendering timeout/duration).
//
// PS2 notes:
// - FUN_00217e18 manipulates globals like cGpffffb6e1, uGpffffad30, uGpffffb6d4/d8
//   (graphics state flags and coordinate references).
// - This opcode provides a simple one-parameter interface to the graphics initiation system,
//   likely used for triggering rendering of pre-configured geometry or setting up view state.
//
// Keep unresolved externs by their original labels for traceability.

#include <stdint.h>

typedef unsigned long long uint64_t;

// VM entry (analyzed name)
extern void bytecode_interpreter(void *result_out); // orig FUN_0025c258

// Graphics pipeline initiator
// - param != 0: copy reference coords to active state, set render flags
// - param == 0: set render duration counter (DAT_0058c0ea = 0x1e)
extern void FUN_00217e18(long param);

// Globals affected by FUN_00217e18 (referenced for documentation):
// - cGpffffb6e1: render state flag (cleared)
// - uGpffffad30, uGpffffad2f: graphics busy/state indicators (cleared)
// - DAT_0058c0a8/ac/b0: active rendering coordinates (set from reference when param != 0)
// - DAT_0055f8d8/dc/e0: reference coordinates (source for copy)
// - uGpffffb6d4/d8/e3: additional render state flags
// - DAT_0058c0ea: render duration/timeout counter (set to 0x1e when param == 0)

// Original signature: undefined8 FUN_0025dfc8(void)
uint64_t opcode_0x45_submit_single_3d_point(void)
{
  int result[4]; // Evaluator output (using array for alignment consistency with VM)

  // Evaluate expression from bytecode stream
  bytecode_interpreter(result);

  // Submit to graphics pipeline initiator
  FUN_00217e18(result[0]);

  return 0;
}

/*
 * Cross-references (globals.json):
 * - No direct global reads/writes in this opcode handler itself
 * - All graphics state changes occur inside FUN_00217e18
 *
 * Usage notes:
 * - Likely paired with opcodes 0x43 (vertex stream builder) or 0x46 (dual coord submission)
 * - Part of cutscene/in-game graphics rendering sequences
 * - Simple wrapper around graphics initiation system
 *
 * TODO:
 * - Analyze FUN_00217e18 further to understand complete graphics pipeline flow
 * - Determine what values are typically passed (coordinate indices vs direct values)
 * - Find example scripts using 0x45 to understand typical usage patterns
 */
