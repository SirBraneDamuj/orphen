// Opcode 0x58 — select_pw_slot_by_index (inferred)
// Original: FUN_0025f0d8 (src/FUN_0025f0d8.c)
//
// Summary
// - Evaluates one expression to an integer index.
// - If index < 0x100, sets the global current pointer DAT_00355044 to
//   (&DAT_0058beb0 + index * 0xEC). Returns 0.
//
// Context
// - Picks a pool slot directly by absolute index (contrast 0x5A which searches by the per-slot +0x4C tag).
// - Stride 0xEC matches the pool object size used by related ops.
//
// Notes
// - Keep original FUN_/DAT_ names for traceability.

#include <stdint.h>

// Expression evaluator
extern void FUN_0025c258(int out[4]);

// Current object pointer
extern uint8_t *DAT_00355044;

// Pool base symbol (byte symbol; treat its address as base pointer)
extern uint8_t DAT_0058beb0;

// Original signature: undefined8 FUN_0025f0d8(void)
void opcode_0x58_select_pw_slot_by_index(void)
{
  int args[4];
  FUN_0025c258(args);
  int idx = args[0];
  if (idx < 0x100)
  {
    DAT_00355044 = &DAT_0058beb0 + idx * 0xEC;
  }
}
