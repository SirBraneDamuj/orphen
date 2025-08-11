// Analyzed Function: Script Pointer Table Accessor
// Original decompiled name: FUN_0025b9e8
// Source file: src/FUN_0025b9e8.c
// Summary:
//   Returns an absolute pointer to an entry within the relocated dialogue/script pointer table.
//   The pointer table base is stored (after relocation) at offset +0x14 from the script base
//   (i.e., it is header index 5). Each table entry is a 32-bit relative offset added to the
//   script base to form an absolute address.
//
// Prototype (inferred):
//   int FUN_0025b9e8(int index);
//
// Behavior:
//   base = FUN_00267f90(uGpffffade0);
//   table_rel_ptr = *(int *)(base + 0x14);        // header[5] relocated value
//   entry_rel = *(int *)(table_rel_ptr + index*4);
//   return entry_rel + base;
//
// Assumptions validated by header analysis:
//   - header was relocated in-place: header[5] already has base added.
//   - pointer table entries themselves remain stored as relative offsets (still need + base).
//   - last entry (sentinel) points to end-of-dialogue region; consumer likely stops earlier.
//
// Future work:
//   - Confirm sentinel logic in consumers (search for comparisons against returned pointer).
//   - Disambiguate whether some entries can be zero (unused slots) or strictly ascending.
//
// Original code (simplified):
//   int FUN_0025b9e8(int param_1) {
//       int b = FUN_00267f90(uGpffffade0);
//       return *(int *)(param_1 * 4 + *(int *)(b + 0x14)) + b;
//   }
//
// Validation procedure in emulator:
//   1. Break after header relocation (see loader file) and record base (B).
//   2. Inspect memory at B+0x14: should contain an absolute address P (pointer_table_start = B + original_header[5]).
//   3. For a small index i, compute: entry = *(uint32_t*)(P + 4*i); absolute = entry + B; verify matches file offset list (relative entry values match raw file offsets for dialogue records).
//
#include <stdint.h>

extern uint32_t uGpffffade0; // script handle
extern int FUN_00267f90(uint32_t handle);

int get_script_pointer_table_entry(int index)
{ // analyzed wrapper
  int base = FUN_00267f90(uGpffffade0);
  int table_base = *(int *)(base + 0x14); // relocated pointer_table_start
  int rel = *(int *)(table_base + index * 4);
  return rel + base;
}
