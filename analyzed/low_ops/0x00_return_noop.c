// Low-opcode 0x00 — return_noop (analyzed)
// Original label: LAB_0025bdc8
//
// Ghidra label body:
//   jr   ra
//   nop
//
// Semantics:
// - No side effects; immediate return from the low-op dispatcher.
// - Consumes no extra bytes beyond the opcode already read by the structural interpreter.
// - Used by the low mini-table at indices 0x00, 0x05, and 0x06 (all point to LAB_0025bdc8), functioning as NOPs.
//
// Notes:
// - Keep FUN_/LAB_ names elsewhere for traceability; this file just documents the behavior.

unsigned int low_opcode_0x00_return_noop(void)
{
  return 0;
}
