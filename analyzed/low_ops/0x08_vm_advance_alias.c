// Low-opcode 0x08 — VM advance (alias of 0x03)
// Original label: LAB_0025beb8
// Raw body: j FUN_0025c220; nop (tail-call / call-terminator)
//
// Semantics:
//   Identical to 0x03: invoke the relative-advance helper to update the main VM pointer
//   (DAT_00355cd0 += *DAT_00355cd0). This low-op uses a tail jump to FUN_0025c220.
//
// Side effects:
//   - Advances the main VM's instruction pointer by the 32-bit delta at its current address.
//
// Notes:
//   - The structural interpreter has already advanced its own pointer past the opcode prior to
//     dispatching this handler. No additional local pointer adjustments occur here.

#include <stdint.h>

// Unanalyzed helper kept by original name for traceability
extern void FUN_0025c220(void);

// Documentation shim mirroring the observed behavior
void lowop_0x08_vm_advance_alias(void)
{
  // Original was a tail-call (j) to FUN_0025c220; we call it directly here
  FUN_0025c220();
}
