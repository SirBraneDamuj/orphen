// Opcode 0x5A — select_pw_by_index (inferred)
// Original: FUN_0025f150 (src/FUN_0025f150.c)
//
// Summary
// - Evaluates one expression argument (target index/id).
// - Scans a fixed pool of objects starting at &DAT_0058d120 with stride 0xEC, in lockstep with an
//   occupancy/status byte array at &DAT_005a96ba.
// - For the first slot where status >= 1 and slot[0x4C] == target, sets the global current pointer
//   DAT_00355044 to that slot and returns 1. If none found after 0xF6 slots, returns 0.
//
// Context
// - 0x51 (set_pw_all_dispatch) seeds the per-object field at +0x4C with the list index it spawned
//   from. This opcode likely selects that spawned object by index for subsequent operations.
// - Return value (0/1) suggests this opcode is used in conditional flows.
//
// Notes
// - Keeps original FUN_/DAT_ names for traceability.
// - We don’t alter the src/ copy; this is a documented wrapper for analysis.

#include <stdint.h>

// Expression evaluator: writes results into provided array; we use [0].
extern void FUN_0025c258(int out[4]);

// Global “current object” pointer updated by this selection.
extern uint8_t *DAT_00355044;

// Object pool base and parallel status array (symbols exported from binary).
extern uint8_t DAT_0058d120; // pool base (byte symbol; treat &DAT_0058d120 as base pointer)
extern uint8_t DAT_005a96ba; // status base (byte symbol)

// Original signature: undefined4 FUN_0025f150(void)
int opcode_0x5A_select_pw_by_index(void)
{
  int args[4];
  FUN_0025c258(args);
  const int target = args[0];

  // Initialize scan pointers
  DAT_00355044 = &DAT_0058d120;          // pool cursor
  const uint8_t *status = &DAT_005a96ba; // status cursor (one byte per slot)

  // Scan up to 0xF6 slots (matches decompiled loop with iVar1 from 0xF5 down to -1)
  for (int i = 0; i <= 0xF5; ++i)
  {
    // Active slot?
    if (*status >= 1)
    {
      // Matches target index/id?
      if (*(int *)(DAT_00355044 + 0x4C) == target)
      {
        return 1; // DAT_00355044 already points to the matched slot
      }
    }

    // Advance to next slot
    DAT_00355044 += 0xEC;
    status += 1;
  }

  // Not found
  return 0;
}
