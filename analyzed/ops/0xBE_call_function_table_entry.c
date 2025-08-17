// Opcode 0xBE — call_function_table_entry (analyzed)
// Original: FUN_00263ee0
//
// Summary:
// - Evaluates two VM expressions:
//   1) index -> selects an entry in a function pointer table at PTR_FUN_0031e730
//   2) arg   -> 32-bit argument passed to the selected function
// - Dispatches: PTR_FUN_0031e730[index](arg).
// - No return value is consumed; treated as a side-effecting call. We return 0 for uniformity.
//
// Notes:
// - No bounds checking observed in the decompiled code; index is assumed valid.
// - The exact calling convention of the target functions is unknown beyond a single u32 param.
// - Keep unresolved externs and original labels for traceability.
//
// Function table PTR_FUN_0031e730 (indexes -> targets):
//   0: FUN_00267210 — compute DAT_0035564c = (sin-like of (DAT_003555b8*1/1024 w/ phase DAT_00352cfc)) / 3
//   1: LAB_00267288 — sb a0, -0x491f(gp): write low byte of arg to a gp-relative small-data global; returns 0
//   2: FUN_00267298 — build UI/menu entry set A from tables at 0x0031e6a8/0x0031e688; triggers FUN_00207de8(0x1007)
//   3: LAB_00267350 — sw a0, DAT_0058c7a8: write 32-bit arg to global at 0x0058c7a8; returns 0
//   4: FUN_00267360 — handle input sequence vs mask table; draws prompt UI when param_1 != 0; updates DAT_0035506e
//   5: FUN_00267650 — small state gate: param 1->FUN_002f2ca0, 2->FUN_002f2d88, else maybe alloc via FUN_00222f70
//   6: FUN_002676d8 — compute time-based scales; write to DAT_0035564c and [DAT_003556e0+7f0/7f4]; set flag at +0x80e
//   7: FUN_002677f8 — build UI/menu entry set B from 0x0031e708/0x0031e6e8; triggers FUN_00207de8(0x1004)
//   8: LAB_002678b0 — debug printf: FUN_0026c088("mp_func(8,%d)\n", arg)
//   9: FUN_002678d8 — cache base positions and compute global angle uGpffffbdc0 from two globals
//  10: FUN_00267928 — rotate globals by (param * fGpffff8d98)/360; updates DAT_0058bf0c, calls FUN_00216a18, updates fGpffffb6d4
//  11: FUN_002679b8 — call FUN_00265f70(0x58beb0) (likely update/apply matrix to object pool)
//  12: LAB_002679e0 — sh a0, -0x53e8(gp): write low 16 bits of arg to a gp-relative small-data global; returns 0
//  13: FUN_002679f0 — calls FUN_00205f80(), which enqueues command 5 with color 0xFFFFFF via FUN_00204ca8 (sema-protected);
//      effect: set/prime the current draw color to white for UI/overlay primitives; ignores arg; commonly paired with mirroring
//      the color into DAT_003569c0 and then uploading the draw state via 0x7240.
//  14: LAB_00267a10 — time/format helper: divides arg by 0x780 (32*60) and uses 0x3C (60); sets a3=-1; likely converts ticks->mm:ss and prints via a UI/debug path (no direct string found in strings.json)

#include <stdint.h>

typedef unsigned int uint;

// VM entry (analyzed name)
extern void bytecode_interpreter(void *result_out); // orig FUN_0025c258

// Function pointer table (raw symbol)
extern void (*PTR_FUN_0031e730[])(unsigned int);

// Original signature: void FUN_00263ee0(void)
unsigned int opcode_0xBE_call_function_table_entry(void)
{
  uint index = 0;
  uint arg = 0;

  bytecode_interpreter(&index);
  bytecode_interpreter(&arg);

  // Indirect call
  PTR_FUN_0031e730[index](arg);

  return 0;
}
