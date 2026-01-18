/*
 * Structural opcode 0x02 — switch/case dispatch (FUN_0025be10)
 *
 * Summary:
 *   Implements a switch/case structure. Evaluates a VM expression to get a selector value,
 *   then searches through a count-prefixed table of [key:int][target:int] pairs.
 *   On match, advances to target. After searching (match or no-match), calls FUN_0025c220.
 *
 * Behavior:
 *   1. Call FUN_0025c258 to evaluate VM expression → selector
 *   2. Read case count byte at piGpffffbd60
 *   3. Align pointer to 4-byte boundary (skip padding if needed)
 *   4. For each case entry:
 *      - Compare selector to case key (first int of pair)
 *      - If match: advance pointer by 4 bytes (to target int), break
 *      - Else: continue to next pair (+8 bytes per entry: key+target)
 *   5. Perform self-relative VM jump using offset at current pointer location
 *      (DAT_00355cd0 += *DAT_00355cd0)
 *
 * Table Structure (inferred):
 *   [count:byte] [padding to align 4] [key0:int32] [target0:int32] [key1:int32] [target1:int32] ...
 *
 * Use Case:
 *   Multi-way branching/dispatch based on computed values. Similar to C switch statements.
 *   The target values are relative offsets used for the self-relative jump.
 *
 * Globals Accessed:
 *   - piGpffffbd60: Instruction stream pointer (int* variant)
 *   - DAT_00355cd0: VM execution pointer (advanced by offset value)
 *
 * External Calls:
 *   - FUN_0025c258: VM expression evaluator
 *
 * Side Effects:
 *   - Advances piGpffffbd60 through case table
 *   - On match: positions pointer at target value before VM advance
 *   - On no-match: pointer ends after all case pairs, then VM advance performed
 *   - DAT_00355cd0 advanced by offset at final pointer position
 *
 * Original: FUN_0025be10
 */

#include <stdint.h>
#include <stdbool.h>

extern int *piGpffffbd60;                 // Instruction stream pointer (int*)
extern int *DAT_00355cd0;                 // VM execution pointer
extern void FUN_0025c258(int *outResult); // VM expression evaluator

void structural_op_0x02_switch_case_dispatch(void)
{
  int selector;

  // Evaluate selector expression
  FUN_0025c258(&selector);

  // Read case count
  char caseCount = *(char *)piGpffffbd60;

  // Align to 4-byte boundary
  uint32_t unaligned = ((uint32_t)piGpffffbd60 + 1) & 3;
  int *caseTable;

  if (unaligned != 0)
  {
    // Add padding to reach next 4-byte boundary
    caseTable = (int *)((uintptr_t)piGpffffbd60 + (5 - unaligned));
  }
  else
  {
    caseTable = (int *)((uintptr_t)piGpffffbd60 + 1);
  }

  piGpffffbd60 = caseTable;

  // Search case table for matching key
  caseCount--; // Convert to 0-based counter

  if (caseCount != -1)
  {
    int currentKey = *piGpffffbd60;

    while (true)
    {
      caseCount--;

      if (selector == currentKey)
      {
        // Match found - advance to target offset
        piGpffffbd60++;
        break;
      }

      // Move to next case pair (key + target = 2 ints)
      piGpffffbd60 += 2;

      if (caseCount == -1)
        break; // No more cases to check

      currentKey = *piGpffffbd60;
    }
  }

  // Perform VM advance with current pointer position
  // After searching the case table, piGpffffbd60 either points to:
  //   - The target offset for a matched case
  //   - Just past the last case entry if no match
  // The VM pointer is then advanced by the offset at that location
  DAT_00355cd0 = (int *)((int)DAT_00355cd0 + *DAT_00355cd0);
}

// Original FUN_0025be10 wrapper
void FUN_0025be10(void)
{
  structural_op_0x02_switch_case_dispatch();
}
