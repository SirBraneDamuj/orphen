// Low-opcode 0x0A — VM advance (alias of 0x03)
// Original label: LAB_0025bed0
// Raw body: j FUN_0025c220; nop (tail-call / call-terminator)
//
// Semantics:
//   Same as 0x03 and 0x08: invoke the relative-advance helper to update the main VM pointer
//   (DAT_00355cd0 += *DAT_00355cd0).
//
// Side effects:
//   - Advances the main VM's instruction pointer by the 32-bit delta at its current address.
//
// Notes:
//   - Structural pointer handling is done by the caller; this only affects the main VM state via FUN_0025c220.

#include <stdint.h>

// Unanalyzed helper kept by original name for traceability
extern void FUN_0025c220(void);

void lowop_0x0A_vm_advance_alias(void)
{
  FUN_0025c220();
}
