/*
 * Load PSC3 Resource (decompressed) - FUN_00222498 (analysis wrapper)
 *
 * Summary
 * -------
 * This function loads a resource described by a small descriptor at param_1.
 * It chooses the archive (MAP.BIN vs SCR.BIN) from a flag nibble, loads to 0x1849a00
 * via FUN_00223268, runs the headerless LZ routine FUN_002f3118 to decode into the
 * working arena (DAT_0035572c), and, when a "compressed PSC3" flag bit is set, asserts
 * the decoded buffer begins with magic 0x33435350 ("PSC3"). It then performs additional
 * relocation/copy steps and returns with pointers/size recorded in the descriptor.
 *
 * Key observations
 * ----------------
 * - Archive select: ((flags >> 5) & 2) → 0 or 2 (0 = SCR?, 2 = MAP)
 * - File id: *(u16*)param_1
 * - Decoding: FUN_002f3118(src=0x1849a00, dst=DAT_0035572c)
 * - PSC3 assertion: if (flags & 1) != 0 then *dst must be 0x33435350 ("PSC3")
 * - Post steps: advance arena by either decoded-size field or total-size, call
 *   FUN_00221f60/FUN_00221e70/FUN_00212058 for relocation/packing, align and bound-check
 *
 * Original: FUN_00222498
 */

#include "orphen_globals.h"
#include <stdint.h>

// External routines (unalayzed names preserved)
extern long FUN_00223268(int archive_type, int file_id, void *io_buffer);
extern void FUN_002f3118(void *src, void *dst);          // headerless LZ decoder used widely
extern void FUN_0026bfc0(unsigned int fmt_or_text_addr); // assert/error
extern void FUN_00221f60(int *decoded, void *arena_end, void *src_buf, int tail_size);
extern void *FUN_00221e70(void *descriptor, int *decoded, void *arena_end);
extern void *FUN_00212058(void *descriptor, int decoded_ptr, void *cur_arena);

// Working arena globals
extern int *DAT_0035572c; // current arena ptr (destination for decodes)
extern int DAT_00355720;  // last transfer size

// Staging buffer base used by loader
#define IO_BUF ((void *)0x1849a00)

// Helper to read descriptor fields (layout inferred from usage)
typedef struct ResourceDesc
{
  uint16_t file_id; // +0x00
  uint16_t pad2;    // +0x02
  uint8_t flags_lo; // +0x04 (low in pair)
  uint8_t flags_hi; // +0x05 (loaded flag stored here)
  uint16_t pad6;    // +0x06 (corresponds to *(byte*)(puVar5+2)) source)
  // ...
  uint32_t out_size; // +0x10 (written: arena_delta)
  int *out_ptr;      // +0x14 (written: decoded pointer)
} ResourceDesc;

void load_psc3_resource(void *param_1 /* original FUN_00222498 signature kept as void* */)
{
  int *arena_ptr = DAT_0035572c;
  ResourceDesc *desc = (ResourceDesc *)param_1;

  // If not loaded yet (flag at +5)
  if (desc->flags_hi == 0)
  {
    // Archive select: ((*(byte*)(puVar5+2) >> 5) & 2)
    uint8_t src_flags = *((uint8_t *)desc + 4); // matches *(byte*)(puVar5+2)
    int archive_type = (src_flags >> 5) & 2;    // 0 or 2

    // Load compressed/packed data to IO buffer
    long ok = FUN_00223268(archive_type, desc->file_id, IO_BUF);
    if (ok < 0)
    {
      FUN_0026bfc0(0x34ba30); // file open/load error
    }

    // Decode to working arena
    FUN_002f3118(IO_BUF, arena_ptr);

    // If LZ-compressed PSC3 flag is set, assert decoded magic is PSC3
    if (((src_flags & 1) != 0) && (*arena_ptr != 0x33435350))
    {
      FUN_0026bfc0(0x34ba78); // unexpected magic after decode
    }

    // Record pointer to decoded data and mark loaded
    *(int **)((uint8_t *)desc + 0x14) = arena_ptr;
    DAT_00355720 = (DAT_00355720 + 0xF) & ~0xF; // align size field already set by decoder path
    desc->flags_hi = 1;

    // Advance arena & relocate as per flags
    if ((src_flags & 1) == 0)
    {
      // Non-PSC3: advance by transfer size
      DAT_0035572c = (int *)((uint8_t *)DAT_0035572c + DAT_00355720);
    }
    else
    {
      // PSC3: advance by a size field stored at decoded[7]
      DAT_0035572c = (int *)((uint8_t *)DAT_0035572c + arena_ptr[7]);
      FUN_00221f60(arena_ptr, DAT_0035572c, IO_BUF, DAT_00355720 - arena_ptr[7]);
    }

    if ((src_flags & 1) != 0)
    {
      void *p = FUN_00221e70(param_1, arena_ptr, DAT_0035572c);
      DAT_0035572c = (int *)p;
      DAT_0035572c = (int *)FUN_00212058(param_1, *(int *)((uint8_t *)desc + 0x14), p);
    }

    // Final align and bounds check
    DAT_0035572c = (int *)(((uintptr_t)DAT_0035572c + 0xF) & ~0xF);
    if (DAT_0035572c > (int *)0x18499ff)
    {
      FUN_0026bfc0(0x34ba88);
    }

    // Store produced size: arena_delta = current - decoded_ptr
    *(int *)((uint8_t *)desc + 0x10) = (int)((uint8_t *)DAT_0035572c - (uint8_t *)*(int **)((uint8_t *)desc + 0x14));
  }
}
