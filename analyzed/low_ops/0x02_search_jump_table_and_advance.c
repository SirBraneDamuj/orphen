// Low-opcode 0x02 — search_jump_table_and_advance (analyzed)
// Original: FUN_0025be10
//
// Summary:
// - Evaluates one VM expression via FUN_0025c258 into value V.
// - Reads a count byte from piGpffffbd60, then aligns piGpffffbd60 to the next 4-byte boundary after that byte.
// - If count != 0xFF (i.e., c-1 != -1), scans up to `count` entries of a table of pairs:
//     Layout per entry: [key:int32][target:int32]
//   It compares V to key for each entry; if matched, advances piGpffffbd60 past the key (to the target field) by one word.
//   Otherwise it skips the whole pair (+= 8) and continues until match or entries exhausted.
// - Finally calls FUN_0025c220 to advance the main VM pointer (DAT_00355cd0 += *DAT_00355cd0).
//
// Effective behavior:
// - Implements a compact switch/jumptable select in the structural stream, keyed by a VM-evaluated value.
// - If no match, leaves piGpffffbd60 positioned after the table and performs the VM advance.
// - If matched, leaves piGpffffbd60 positioned on the matched target word (one past the key) before the VM advance.
//
// Notes:
// - Keeps raw FUN_/DAT_ names (piGpffffbd60, FUN_0025c258, FUN_0025c220) for traceability.

#include <stdint.h>

// Raw implementation we are wrapping
extern void FUN_0025be10(void);

unsigned int low_opcode_0x02_search_jump_table_and_advance(void)
{
  FUN_0025be10();
  return 0;
}
