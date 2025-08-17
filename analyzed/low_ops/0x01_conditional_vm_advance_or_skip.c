// Low-opcode 0x01 — conditional_vm_advance_or_skip (analyzed)
// Original: FUN_0025bdd0
//
// Summary:
// - Evaluates one VM expression via FUN_0025c258 into a local.
// - If the value == 0, calls FUN_0025c220 (advance main VM pointer: DAT_00355cd0 += *DAT_00355cd0).
// - Else (non-zero), advances the VM scratch/base pointer by 4: iGpffffbd60 += 4.
//
// Structural notes:
// - Consumes no additional bytes from the structural stream (pbGpffffbd60 already advanced by caller).
// - Acts as a tiny guard in block prologues: zero => follow VM chain; non-zero => skip a 4-byte cell in the aux stream.
// - Keeps raw FUN_/DAT_ names for traceability; this file documents observed behavior.

#include <stdint.h>

// Raw implementation we are wrapping
extern void FUN_0025bdd0(void);

unsigned int low_opcode_0x01_conditional_vm_advance_or_skip(void)
{
  FUN_0025bdd0();
  return 0;
}
