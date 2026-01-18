/*
 * Structural opcode 0x01 — conditional VM advance (FUN_0025bdd0)
 *
 * Summary:
 *   Evaluates a VM expression. If result is 0, advances VM pointer via FUN_0025c220.
 *   Otherwise, skips ahead 4 bytes in the instruction stream (iGpffffbd60 += 4).
 *
 * Behavior:
 *   1. Call FUN_0025c258(outBuffer) to evaluate VM expression → result
 *   2. If result == 0:
 *      - Call FUN_0025c220() to perform relative VM pointer advance
 *   3. Else:
 *      - Add 4 to iGpffffbd60 (skip 4 bytes, likely a jump offset or data block)
 *
 * Use Case:
 *   Conditional execution/branching in structural script flow. The 4-byte skip
 *   likely corresponds to a 32-bit offset or address that's only consumed when
 *   the condition is false.
 *
 * Globals Accessed:
 *   - iGpffffbd60: Instruction stream pointer (modified on false branch)
 *
 * External Calls:
 *   - FUN_0025c258: VM expression evaluator (returns int result)
 *   - FUN_0025c220: Relative VM advance (adjusts DAT_00355cd0)
 *
 * Side Effects:
 *   - On true (result==0): VM pointer advanced via FUN_0025c220
 *   - On false: iGpffffbd60 skips 4 bytes
 *
 * Original: FUN_0025bdd0
 */

#include <stdint.h>

extern int iGpffffbd60;                   // Instruction stream pointer
extern int *DAT_00355cd0;                 // VM execution pointer
extern void FUN_0025c258(int *outResult); // VM expression evaluator

void structural_op_0x01_conditional_vm_advance(void)
{
  int result;

  // Evaluate VM expression
  FUN_0025c258(&result);

  if (result == 0)
  {
    // True branch: perform self-relative jump
    // Advances VM pointer by offset stored at current location
    DAT_00355cd0 = (int *)((int)DAT_00355cd0 + *DAT_00355cd0);
  }
  else
  {
    // False branch: skip 4 bytes (likely a jump offset/data block)
    // The structural stream pointer bypasses embedded data without executing it
    iGpffffbd60 += 4;
  }
}

// Original FUN_0025bdd0 wrapper
void FUN_0025bdd0(void)
{
  structural_op_0x01_conditional_vm_advance();
}
