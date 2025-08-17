// Low-opcode 0x03 — vm_advance (analyzed)
// Original label: LAB_0025bea0
//
// Ghidra label body:
//   j   FUN_0025c220
//   nop
// (flow override: CALL_RETURN)
//
// Semantics:
// - Thin trampoline into FUN_0025c220, which performs a self-relative jump on the main VM stream:
//     DAT_00355cd0 = DAT_00355cd0 + *DAT_00355cd0;
// - Consumes no bytes from the structural stream; it manipulates the separate VM pointer/cell.
// - Used by the structural mini-dispatch table (PTR_LAB_0031e1f8) at index 0x03.
//
// Notes:
// - Keep the raw FUN_/DAT_ names for traceability; this file documents the behavior and provides a named shim.

#include <stdint.h>

// External: main VM advance helper
extern void FUN_0025c220(void);

// Named shim for analysis; not necessarily wired by the runtime
unsigned int low_opcode_0x03_vm_advance(void)
{
  FUN_0025c220();
  return 0;
}
