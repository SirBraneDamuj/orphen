// Opcode 0x59 — get_pw_slot_index (inferred)
// Original: FUN_0025f120 (src/FUN_0025f120.c)
//
// Summary
// - Returns the absolute pool index of the currently selected object (DAT_00355044).
// - If no object is selected (DAT_00355044 == 0), returns 0x100.
//
// Details
// - Pool base is &DAT_0058beb0 with stride 0xEC. The raw code uses a magic multiply and shift to
//   implement division by 0xEC when converting pointer->index. Semantically:
//     idx = (DAT_00355044 - &DAT_0058beb0) / 0xEC;  // when DAT_00355044 != 0
// - This opcode is an expression atom: FUN_0025c258 pushes its return value on the eval stack.
//
// Streams/consumption
// - Main VM: 1 byte (the opcode itself). No immediate.
// - Param stream: none.
//
// Related
// - 0x58 selects by absolute index (sets DAT_00355044 to base + idx*0xEC).
// - 0x5A selects by tag stored at slot[+0x4C].
//
// Keep original FUN_/DAT_ names for traceability.

#include <stdint.h>

extern uint8_t *DAT_00355044; // current object pointer
extern uint8_t DAT_0058beb0;  // pool base symbol

// Original signature: int FUN_0025f120(void)
int opcode_0x59_get_pw_slot_index(void)
{
  if (DAT_00355044 == 0)
    return 0x100;
  // Pointer-difference in bytes divided by stride 0xEC
  intptr_t diff = (intptr_t)(DAT_00355044 - &DAT_0058beb0);
  return (int)(diff / 0xEC);
}
