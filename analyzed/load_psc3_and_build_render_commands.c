// Analyzed from: src/FUN_00222498.c
// Original symbol: FUN_00222498
//
// Purpose
//   High-level loader for PSC3 resources used in map segments. Reads a segment from storage
//   into a staging buffer, optionally LZ-decompresses into the decoded arena, relocates internal
//   section pointers, performs optional extended setup, and finally builds a GPU/engine command
//   buffer for rendering by invoking the renderer/command-builder.
//
// Key PS2/engine details
// - Staging buffer: 0x001849A00 (uncached KSEG1 address space is implied by caller usage).
// - Decoded arena pointer: DAT_0035572c (global moving pointer). Always maintained 16-byte aligned.
// - Decoded size of last operation: DAT_00355720 (bytes produced by decoder or direct copy).
// - Compression: Custom LZ-like routine at FUN_002f3118; when the request flag indicates
//   "compressed PSC3", the loader asserts PSC3 magic (0x33435350) after decode.
// - Relocation: PSC3 on-disk stores internal section fields as offsets relative to the original base.
//   After decode/copy, FUN_00221f60 copies to the final arena address and adds the base delta
//   to four u32 header fields at offsets +0x1C, +0x20, +0x24, +0x28.
// - Extended setup: If request flag bit 0 is set and header +0x40 is non-zero, FUN_00221e70 builds
//   four initialized tables appended into the arena; their pointers are written back into the request
//   struct at offsets +0x18, +0x1C, +0x20, +0x24.
// - Command buffer build: FUN_00212058 consumes the PSC3 structure (section pointers at +0x08, +0x1C,
//   +0x24) and emits a sequence of render/state commands into the arena at the current pointer.
//   The request struct’s field at +0x28 is also updated to reference this command buffer.
//
// Request struct (partial, inferred offsets)
//   +0x00: u16 group/segment id (passed to resource reader)
//   +0x04: u8 flags
//          bit0: PSC3 path enabled (also gates extended setup)
//          bit2: set by FUN_00221e70 when it re-bases to header+0x40 subregion
//   +0x05: u8 loaded flag (set to 1 after first successful load/decode)
//   +0x18..+0x24: u32 pointers – outputs from FUN_00221e70 (4 tables)
//   +0x28: u32 command buffer pointer – set by FUN_00212058
//   +0x20: u32 size consumed (written by this loader at end)
//   +0x28: u32 base pointer to decoded PSC3 (written early)
//
// PSC3 header (partial, from downstream consumption)
//   +0x00: 'PSC3' magic (0x33435350)
//   +0x04: u16 submesh_count
//   +0x06: u16 reserved
//   +0x08: u32 offs_submesh_list (N entries x 0x14); entry[+6] used to compute max streams
//   +0x1C: u32 offs_draw_desc_table (entries x 0x18); used heavily by renderer
//   +0x20: u32 offs_section_B (relocated; not consumed in FUN_00212058)
//   +0x24: u32 offs_resource_table (entries x 10 bytes); indexed by draw_desc stream refs
//   +0x28: u32 offs_section_D (relocated; not consumed in FUN_00212058)
//   +0x40: u32 offs_subheader (optional; triggers extended setup)
//   +0x1C (dword index 7): u32 total_size_reserved (used when compressed path is active)
//
// Contract (inputs/outputs)
// - Input: param_1: pointer to request struct (opaque to us; see offsets above)
// - Effect: Advances DAT_0035572c by the total space used (PSC3 + optional tables + command buffer),
//           writes base pointer and size into request, and enqueues render commands into arena.
// - Errors: Calls FUN_0026bfc0 with debug strings on read/decode/overflow failures.
//
// NOTE: Keep original FUN_* names for callees until analyzed separately.

#include <stdint.h>

// Globals (externed here for documentation; defined in game)
extern int *DAT_0035572c; // decoded arena cursor (16B aligned)
extern int DAT_00355720;  // bytes written by last decode/copy

// Callees (unanalyzed here; keep original names)
extern long FUN_00223268(long groupKind, long segmentId, uint32_t destAddr /*0x1849A00*/);
extern void FUN_002f3118(uint8_t *src /*0x1849A00*/, uint8_t *dst /*DAT_0035572c*/);
extern void FUN_0026bfc0(uint32_t fmtStringAddr, ...);
extern void FUN_00221f60(int psc3Base, void *newBase, void *oldBase, int size);
extern uint32_t FUN_00221e70(int request, int psc3Base, int arenaPtr);
extern uint32_t *FUN_00212058(int request, int psc3Base, uint32_t *outCmdBuf);

// Entry point (signature preserved via comment only)
// void FUN_00222498(undefined8 param_1)
void load_psc3_and_build_render_commands(void *requestOpaque /* param_1 */)
{
  // Types are intentionally loose to avoid overcommitting to struct layouts.
  uint8_t *request = (uint8_t *)requestOpaque;
  int *arenaBaseBefore = DAT_0035572c;

  // Loaded guard at +5
  if (request[5] != 0)
    return;

  // Read segment into staging buffer 0x1849A00; group kind is bits 5.. of byte at +4
  uint8_t reqFlags = request[4];
  long groupKind = (reqFlags >> 5) & 2;  // matches decompiled mask
  long segmentId = *(uint16_t *)request; // request +0x00
  long r = FUN_00223268(groupKind, segmentId, 0x1849A00);
  if (r < 0)
  {
    FUN_0026bfc0(0x34ba30); // "segment read failed" (see strings.json)
  }

  // Decode (or direct copy) into arena; decoder sets DAT_00355720 (produced size)
  FUN_002f3118((uint8_t *)0x1849A00, (uint8_t *)arenaBaseBefore);

  // If PSC3 path flag (bit0) is set, assert magic in compressed path
  if ((reqFlags & 1) != 0)
  {
    if (arenaBaseBefore[0] != 0x33435350)
    {                         // 'PSC3'
      FUN_0026bfc0(0x34ba78); // bad magic
    }
  }

  // Publish PSC3 base to request at +0x28 (puVar5+10)
  *(int **)(request + 0x28) = arenaBaseBefore;

  // Align decoded size to 16B and set loaded flag
  DAT_00355720 = (DAT_00355720 + 0xF) & ~0xF;
  request[5] = 1;

  // Advance arena and relocate if compressed path is active
  if ((reqFlags & 1) == 0)
  {
    // Uncompressed: consume aligned size
    DAT_0035572c = (int *)((uintptr_t)DAT_0035572c + DAT_00355720);
  }
  else
  {
    // Compressed PSC3: consume reserved size from header dword[7] at +0x1C
    int reservedSize = arenaBaseBefore[7];
    DAT_0035572c = (int *)((uintptr_t)DAT_0035572c + reservedSize);
    // Relocate: copy bytes and add base delta to 4 header fields
    FUN_00221f60((int)arenaBaseBefore, DAT_0035572c, (void *)0x1849A00, DAT_00355720 - reservedSize);
  }

  // Extended setup and command buffer build (only when PSC3 path bit0 is set)
  if ((reqFlags & 1) != 0)
  {
    uint32_t next = FUN_00221e70((int)request, (int)arenaBaseBefore, (int)DAT_0035572c);
    DAT_0035572c = (int *)next;
    DAT_0035572c = (int *)FUN_00212058((int)request, *(int *)(request + 0x28), (uint32_t *)next);
  }

  // Final align and overflow guard against end of arena (0x18499FF upper bound from code)
  DAT_0035572c = (int *)(((uintptr_t)DAT_0035572c + 0xF) & ~0xF);
  if ((int *)0x18499FF < DAT_0035572c)
  {
    FUN_0026bfc0(0x34ba88); // arena overflow
  }

  // Record total size consumed in request at +0x20 (puVar5+8)
  *(int *)(request + 0x20) = (int)((uintptr_t)DAT_0035572c - (uintptr_t)*(int **)(request + 0x28));
}
