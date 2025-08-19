// Opcode 0x4D — Process resource ID list from stream
// Original: FUN_0025e628
//
// Summary
// - Reads a count byte from the VM byte stream, allocates a small temporary buffer
//   in the 0x70000000 scratch arena, fills it with `count` 16-bit IDs read via
//   FUN_0025c1d0 (low 16 bits of each), appends a 0 terminator, and calls
//   FUN_002661f8(list) to process the list.
// - Negative IDs are skipped by the walker; non-negative IDs are resolved and
//   handled by FUN_002661a8 (via FUN_002661f8 -> FUN_002661a8).
// - The temporary arena allocation is rolled back before returning.
// - Returns 0.
//
// Arena math
// - Alloc blocks = ceil((count + 1) * 2 / 16) = ceil((count + 1)/8) blocks of 16 bytes.
//   This matches the decompiled pattern: (count + 8) >> 3, then * 0x10.
// - Bounds check: if the arena pointer exceeds 0x70003FFF, trap via FUN_0026bf90(0).
//
// Notes
// - Keep original FUN_/DAT_ names for traceability; callees remain unanalyzed.
// - This likely preloads or registers a batch of resource IDs referenced by scripts.

#include <stdint.h>

// VM byte pointer (instruction stream)
extern unsigned char *DAT_00355cd0;

// Scratch arena base pointer (address encoded as an int)
extern int DAT_70000000;

// Helpers
extern uint32_t FUN_0025c1d0(void);    // read 32-bit from stream, advance pointer
extern void FUN_0026bf90(int);         // error/abort
extern void FUN_002661f8(short *list); // walk list: 0-terminated; negatives skipped; positives dispatched

// NOTE: Original signature: undefined8 FUN_0025e628(void)
void opcode_0x4D_process_resource_id_list(void)
{
  // Read count
  uint8_t count = *DAT_00355cd0;
  DAT_00355cd0++;

  // Compute 16-byte blocks for (count entries + terminator), each 2 bytes
  int allocBlocks = ((int)count + 8) >> 3; // equals ceil((count + 1)/8)
  int oldArena = DAT_70000000;
  DAT_70000000 = oldArena + allocBlocks * 0x10;
  if (DAT_70000000 > 0x70003fff)
  {
    FUN_0026bf90(0);
  }

  // Fill list with low 16 bits of each 32-bit value
  uint16_t *list = (uint16_t *)(uintptr_t)oldArena;
  for (uint32_t i = 0; i < count; ++i)
  {
    uint32_t v = FUN_0025c1d0();
    list[i] = (uint16_t)v;
  }
  list[count] = 0; // terminator

  // Process the list
  FUN_002661f8((short *)list);

  // Roll back arena
  DAT_70000000 = DAT_70000000 - allocBlocks * 0x10;
}
