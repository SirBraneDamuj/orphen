// write_swizzled_bank_stream_and_mark_dirty (analyzed)
// Original: FUN_00210b60
//
// Purpose:
// - Copy a linear stream of 32-bit words into a per-bank, GS-friendly swizzled/tiled
//   staging buffer, then mark the bank "dirty" for this frame and enqueue a DMA/GIF upload
//   if not already done.
//
// Signature (from decompiled):
//   void FUN_00210b60(undefined8 bank, uint startWord, int lengthWords, undefined4 *srcWords)
// Interpreted types:
//   bank: selects the staging buffer instance (0..N-1)
//   startWord: starting index in destination (in words)
//   lengthWords: number of 32-bit words to write
//   srcWords: pointer to source words
//
// Behavior details:
// - Destination base per bank: 0x4fefe0 + bank * 0x460 bytes
//   • 0x460 bytes = 70 QWC (QuadWord Count), matches DMA tags in FUN_00210ac8 (0x46)
// - Address swizzle: maps linear word index i into a tiled order used by the GS upload.
//   destWordIndex = ((((i & 0xF) >> 3) + ((i >> 4) & 0xFE)) * 0x10)
//                    + (i & 0x7)
//                    + (((i >> 4) & 0x1) * 0x8)
//   This interleaves 8-word groups across 16-word tiles with alternating half-rows.
// - Dirty marking & DMA scheduling:
//   • Per-bank dirty stamp array at DAT_004fee80[bank] is compared with iGpffffb644 (current frame stamp).
//   • On first write per frame, updates DAT_004fee80[bank] and calls FUN_00210ac8(bank, 0) to enqueue
//     a DMA chain pointing at &DAT_004fef80 + bank*0x460.
//
// PS2 specifics (inferred):
// - FUN_00210ac8 builds DMAC tags (IDs 3=REF, 5=CNT) with QWC=0x46 and sets the source to the bank buffer.
// - The two close bases 0x4fef80 and 0x4fefe0 are used for DMA pointer and CPU staging respectively.
//
// Notes:
// - This analyzed implementation mirrors the decompiled logic and keeps unresolved externs by original names.
// - Callers (e.g., opcode 0xE4) usually provide a script-relative pointer resolved via DAT_00355058.

#include <stdint.h>

typedef unsigned int uint;

// Staging buffers and per-bank state (extern as raw symbols, types inferred)
extern unsigned int DAT_004fee80[];  // per-bank dirty/frame stamp (stores frame stamp as u32)
extern unsigned char DAT_004fef80[]; // base used by DMA enqueue (see FUN_00210ac8)
extern unsigned char DAT_004fefe0[]; // CPU staging base actually written by this function
extern unsigned int iGpffffb644;     // current frame stamp

// Low-level enqueue for DMA/GIF upload
extern void FUN_00210ac8(int bank, int subIndex);

static inline uint tile_swizzle_word_index(uint i)
{
  uint lowNib = i & 0xF;
  uint high = i >> 4;
  uint res = (((lowNib >> 3) + (high & 0xFE)) * 0x10) + (i & 0x7) + ((high & 0x1) * 0x8);
  return res;
}

void write_swizzled_bank_stream_and_mark_dirty(unsigned long long bank,
                                               unsigned int startWord,
                                               int lengthWords,
                                               const unsigned int *srcWords)
{
  uint endWord = startWord + (uint)lengthWords;
  uint i = startWord;

  // Per-bank destination base (CPU staging region at 0x4fefe0)
  unsigned int *dstBase = (unsigned int *)(DAT_004fefe0 + (size_t)bank * 0x460);

  while ((int)i < (int)endWord)
  {
    uint swz = tile_swizzle_word_index(i);
    dstBase[swz] = *srcWords++;
    i++;
  }

  // Mark dirty and enqueue DMA on first update this frame
  if (DAT_004fee80[(int)bank] != iGpffffb644)
  {
    DAT_004fee80[(int)bank] = iGpffffb644;
    FUN_00210ac8((int)bank, 0);
  }
}
