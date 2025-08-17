// Opcode 0xE4 — stage_swizzled_word_stream_upload (analyzed)
// Original: FUN_00265148
//
// Summary:
// - Evaluates four VM expressions (in order):
//   1) bank/index (selects a per-bank upload buffer)
//   2) start offset (in 32-bit words)
//   3) length/count (number of 32-bit words)
//   4) data pointer expressed as a script-relative offset
// - Calls FUN_00210b60(bank, start, length, base + offset), which:
//   • Writes the given word stream into a per-bank, GS-friendly swizzled/tiled buffer at
//     &DAT_004fef80[bank] with stride 0x460 (see FUN_00210b60 addressing formula).
//   • Marks the bank as dirty and enqueues a DMA/VIF upload via FUN_00210ac8 on first update
//     for that bank in a frame (guarded by DAT_004fee80[bank] vs iGpffffb644).
//
// Notes:
// - DAT_00355058 is a global base pointer used throughout the VM as the current script/resource base.
// - This opcode only stages and schedules the upload; it returns 0.
// - Keep unresolved externs with their original names for traceability.

#include <stdint.h>

typedef unsigned int uint;

// VM entry (analyzed name)
extern void bytecode_interpreter(void *result_out); // orig FUN_0025c258

// Script/resource base pointer used to resolve relative offsets
extern unsigned char *DAT_00355058;

// Low-level swizzled write + DMA trigger (analyzed name; orig FUN_00210b60)
extern void write_swizzled_bank_stream_and_mark_dirty(unsigned long long bank,
                                                      unsigned int startWord,
                                                      int lengthWords,
                                                      unsigned int *srcWords);

// Original signature: undefined8 FUN_00265148(void) — returns 0
unsigned int opcode_0xE4_stage_swizzled_word_stream_upload(void)
{
  // Evaluate operands via VM
  uint bank = 0;
  uint start = 0;
  uint length = 0;
  uint relOffset = 0;

  bytecode_interpreter(&bank);
  bytecode_interpreter(&start);
  bytecode_interpreter(&length);
  bytecode_interpreter(&relOffset);

  // Resolve script-relative pointer and dispatch to the uploader
  unsigned int *src = (unsigned int *)(DAT_00355058 + relOffset);
  // Call analyzed helper (orig FUN_00210b60)
  write_swizzled_bank_stream_and_mark_dirty(bank, start, (int)length, src);

  return 0; // consistent with original
}
