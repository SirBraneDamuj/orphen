// Select current object frame — helper used by many opcodes
// Original: void FUN_0025d6c0(long param_1, undefined4 param_2) in src/FUN_0025d6c0.c
//
// Summary
//   Sets the global current object pointer (DAT_00355044) either by selecting an indexed
//   entry from a global object/frame pool at DAT_0058beb0 with stride 0xEC, or by directly
//   assigning a provided pointer when a sentinel selector value is used.
//
// Semantics
//   - If selector != 0x100 (256):
//       DAT_00355044 = (&DAT_0058beb0) + selector * 0xEC
//     This chooses the Nth object/frame in a contiguous table.
//   - Else (selector == 0x100):
//       DAT_00355044 = fallbackPtr
//     This overrides the selection with an explicit pointer supplied by the caller.
//
// Notes
//   - No bounds checking is performed on selector; invalid indices can point outside the pool.
//   - The 0xEC stride matches the size of one object/frame struct observed elsewhere.
//   - Frequently used by opcodes (e.g., 0x64, 0x76–0x7C) to set the active context for
//     subsequent register reads/writes or transforms.
//
// Keep the original FUN_* name in comments for traceability and provide a thin wrapper.

#include <stdint.h>
#include <stddef.h>

// Raw globals
extern void *DAT_00355044;         // current object pointer
extern unsigned char DAT_0058beb0; // base of object/frame pool (byte-typed for pointer arithmetic)

// Analyzed, descriptive entry point
void select_current_object_frame(uint32_t selector, void *fallbackPtr)
{
  if (selector != 0x100)
  {
    // Compute address: base + selector * 0xEC
    DAT_00355044 = (void *)((uintptr_t)&DAT_0058beb0 + (uintptr_t)selector * 0xECu);
    return;
  }
  // Directly set to provided pointer when sentinel used
  DAT_00355044 = fallbackPtr;
}

// Original signature wrapper
// Original: void FUN_0025d6c0(long param_1, undefined4 param_2)
void FUN_0025d6c0(long param_1, uint32_t param_2)
{
  select_current_object_frame((uint32_t)param_1, (void *)(uintptr_t)param_2);
}
