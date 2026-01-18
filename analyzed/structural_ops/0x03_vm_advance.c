/*
 * Structural opcode 0x03 — VM advance (LAB_0025bea0)
 *
 * Summary:
 *   Unconditionally calls FUN_0025c220 to advance the VM pointer.
 *
 * Behavior:
 *   Simply calls FUN_0025c220(), which performs a relative jump by reading an offset
 *   and updating DAT_00355cd0 (the VM execution pointer).
 *
 * Use Case:
 *   Unconditional forward jump in structural script flow. The offset to jump by
 *   is embedded in the instruction stream and consumed by FUN_0025c220.
 *
 * Relationship to Other Opcodes:
 *   - Opcodes 0x08 and 0x0A are aliases (same implementation via tail call)
 *
 * External Calls:
 *   - FUN_0025c220: Relative VM advance (reads offset from DAT_00355cd0)
 *
 * Side Effects:
 *   - VM execution pointer (DAT_00355cd0) advanced by offset value
 *
 * Original: LAB_0025bea0 (inline label in FUN_0025bc68)
 * Also used by: structural opcodes 0x08, 0x0A
 */

// Globals
extern int *DAT_00355cd0; // VM execution pointer

void structural_op_0x03_vm_advance(void)
{
  // Perform self-relative jump: advance pointer by the offset stored at current location
  // This implements: DAT_00355cd0 = (int*)((int)DAT_00355cd0 + *DAT_00355cd0)
  //
  // The current cell contains a 32-bit offset value (likely signed). This offset is added
  // to the current pointer address to compute the next execution location.
  //
  // Example:
  //   If DAT_00355cd0 points to address 0x1000, and the value at 0x1000 is 0x5D (93 bytes),
  //   then after this operation DAT_00355cd0 will point to 0x1000 + 0x5D = 0x105D
  //
  // This is used for structured blocks where the offset represents the span/length
  // of the block body or jump distance to the next code segment.

  DAT_00355cd0 = (int *)((int)DAT_00355cd0 + *DAT_00355cd0);
}
