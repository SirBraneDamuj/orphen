// Analyzed from: src/FUN_00221e70.c, src/FUN_002256d0.c, src/FUN_002256f0.c
// Original symbols: FUN_00221e70, FUN_002256d0, FUN_002256f0
//
// Purpose
//   Optional post-relocation setup when a PSC3 subheader is present at header+0x40 and the
//   request’s bit0 flag is set. Re-bases to the subheader region, then constructs four tables
//   back-to-back in the arena. Each table has:
//     - u32 sourceTablePtr (points back to the subheader region)
//     - count entries of 10 bytes each, initialized with default values
//   The size for each table is: align4(((count - 1) * 10) + 3) + 0x10
//   Pointers to the four constructed tables are written back into the request at offsets
//   +0x18, +0x1C, +0x20, +0x24. The request’s flags (+0x04) also OR with 0x04 on success.
//
// Notes
// - The subheader pointer is treated as a single base; all four tables use the same count value
//   read from *(short*)subheaderBase. This matches the decompiled code which does not advance the
//   subheader pointer between iterations.
// - The function returns the next 16-byte aligned arena pointer after emitting all four tables.
//
// Contract
// - Input param_1: request pointer (opaque; flags at +0x04, outputs at +0x18..+0x24)
// - Input param_2: PSC3 base pointer (int)
// - Input param_3: current arena pointer (int)
// - Output: next aligned arena pointer (int)

#include <stdint.h>

static int calc_block_size_from_count(const int16_t *subheaderBase)
{
  // FUN_002256d0: return (((count-1)*10 + 3) & ~3) + 0x10
  int count = *subheaderBase;
  return (((count - 1) * 10 + 3) & ~3) + 0x10;
}

static void init_table_records(const int16_t *subheaderBase, uint32_t *dst)
{
  // FUN_002256f0: first u32 = source pointer; then count x 10-byte records with defaults
  int count = *subheaderBase;
  dst[0] = (uint32_t)(uintptr_t)subheaderBase; // source table pointer
  uint8_t *p = (uint8_t *)&dst[1];
  for (int i = 0; i < count; i++)
  {
    // 10-byte record layout (observed defaults): [u16,u16,u16,u8(0xFF),u8(1),u8(0),u8(0)]
    // matches decompiled stores: 0,0,0,0xFF,1,0,0 per entry
    *(uint16_t *)(p + 0) = 0;
    *(uint16_t *)(p + 2) = 0;
    *(uint16_t *)(p + 4) = 0;
    *(uint8_t *)(p + 6) = 0xFF;
    *(uint8_t *)(p + 7) = 1;
    *(uint8_t *)(p + 8) = 0;
    *(uint8_t *)(p + 9) = 0;
    p += 10;
  }
}

// uint FUN_00221e70(int param_1,int param_2,int param_3)
uint32_t psc3_extended_setup_tables(void *requestOpaque, int psc3Base, uint32_t arenaPtr)
{
  uint8_t *request = (uint8_t *)requestOpaque;
  // Gate on request bit0 and header+0x40 presence
  if ((request[4] & 1) != 0)
  {
    int offs40 = *(int *)(psc3Base + 0x40);
    if (offs40 != 0)
    {
      const int16_t *subheaderBase = (const int16_t *)(psc3Base + offs40);
      request[4] |= 0x04; // mark that extended region was used

      uint32_t *outPtr = (uint32_t *)(request + 0x18); // 4 outputs
      uint32_t cursor = arenaPtr;
      for (int i = 0; i < 4; i++)
      {
        int size = calc_block_size_from_count(subheaderBase);
        init_table_records(subheaderBase, (uint32_t *)cursor);
        outPtr[i] = cursor;
        cursor += size;
      }
      // Return 16-byte aligned next pointer
      return (cursor + 0xF) & ~0xF;
    }
  }
  // No-op: just align and return
  return (arenaPtr + 0xF) & ~0xF;
}
