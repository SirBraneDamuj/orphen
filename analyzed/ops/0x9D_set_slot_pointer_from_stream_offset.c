// Opcode 0x9D — Set slot pointer from stream offset
// Original: FUN_00261cb8
//
// Summary
// - Parses an index expression, reads a 32-bit offset from the bytecode stream, and writes
//   (iGpffffb0e8 + offset) into the slot table entry at iGpffffbd84[index]. Bounds check is 0x40 slots.
//
// Decoding details
// - FUN_0025c258(aiStack_20): expression evaluator; aiStack_20[0] yields the target slot index
// - FUN_0025c1d0(): reads a u32 from main VM stream (DAT_00355cd0) and advances it by 4 bytes
// - iGpffffb0e8: base pointer for script/resource segment (gp-relative)
// - iGpffffbd84: base of slot table (array of 4-byte pointers/handles)
// - On out-of-range index (>= 0x40), calls FUN_0026bfc0(0x34d148) (likely an assert/log)
//
// Notes
// - Pairs with opcode 0x9E (finish_process_slot), which clears a slot. This one assigns a pointer for the slot.
// - Many nearby ops (e.g., 0x61E30) similarly write base+offset into per-index structures.
//
// Original signature: undefined8 FUN_00261cb8(void)
// Returns 0.

#include <stdint.h>

// Externs retained with original names for traceability
extern void FUN_0025c258(int out4[4]); // bytecode expression evaluator
extern int FUN_0025c1d0(void);         // read u32 from DAT_00355cd0 and advance
extern int iGpffffbd84;                // slot table base (gp-relative address)
extern int iGpffffb0e8;                // base for script/resource segment
extern void FUN_0026bfc0(int code);    // error/assert/log helper

void opcode_0x9D_set_slot_pointer_from_stream_offset(void)
{
  int tmp[4];
  FUN_0025c258(tmp);
  int index = tmp[0];
  int offset = FUN_0025c1d0();

  if ((unsigned)index < 0x40u)
  {
    *(int *)(iGpffffbd84 + index * 4) = iGpffffb0e8 + offset;
  }
  else
  {
    FUN_0026bfc0(0x34d148);
  }
}
