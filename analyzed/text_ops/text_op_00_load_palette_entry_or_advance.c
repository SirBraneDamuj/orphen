// Text opcode 0x00 — FUN_00239178
// Behavior:
//   Maintains an index (DAT_00355c64). If index < 8, loads a 4-byte value from a table at &DAT_005716a0
//   (offset = index * 4) into DAT_00354e30, then increments the index. Once index reaches 8, instead of
//   loading further entries it calls FUN_002663a0(0x8FE) and sets bit 0x2000 in DAT_00355064.
//
// Inferred Role:
//   Acts as a palette / parameter sequence initializer for subsequent text operations. DAT_00354e30 is
//   dereferenced by other text op handlers (e.g., opcode 0x01 color selection, opcode 0x11 / FUN_002395c0
//   reading structured bytes). After up to 8 loads, further invocations flip a global state bit to signal
//   exhaustion or completion.
//
// Globals touched:
//   DAT_00355c64 (uint)   : load index (0..8). Incremented until 8.
//   DAT_00354e30 (void*)  : pointer/value loaded from table entries (&DAT_005716a0 + index*4).
//   DAT_00355064 (uint16?) : bitfield OR with 0x2000 upon exhaustion.
//   &DAT_005716a0         : base of 8-entry table (32 bytes) of pointers/values.
//
// External calls:
//   FUN_002663a0(0x8FE) — likely a notifier/log/event when attempting to read beyond available entries.
//
// Notes:
//   - DAT_00354e30 is treated as a pointer to byte data in opcode 0x11 (FUN_002395c0) and as a byte flag in
//     opcode 0x01 color choice (*DAT_00354e30 == 1 check). Thus each table element probably references a
//     structure or block rather than being an immediate color.
//   - Bit 0x2000 in DAT_00355064 may mark “palette/parameter table exhausted” or enable a fallback path.
//
// TODO:
//   - Name constants: TEXT_PARAM_TABLE_BASE (&DAT_005716a0), TEXT_PARAM_MAX_ENTRIES (8), STATE_TEXT_PARAM_DONE (0x2000).
//   - Confirm width of DAT_00355064 (16 vs 32 bit) and unify bitmask definitions across related analyzed files.
//
// Side effects: updates index and global pointer, potentially sets a state flag after 8 calls.
// No parameters; no return value.

#include <stdint.h>

extern unsigned int DAT_00355c64; // load index
extern void *DAT_00354e30;        // active parameter block pointer
extern unsigned int DAT_00355064; // state flags bitfield (size tentative)
extern unsigned int DAT_005716a0; // table base (treated as array of 4-byte entries)

extern void FUN_002663a0(unsigned int code); // notifier/log

void text_op_00_load_palette_entry_or_advance(void)
{
  if (DAT_00355c64 < 8)
  {
    unsigned int offset = DAT_00355c64 * 4;
    DAT_00355c64++;
    DAT_00354e30 = *(void **)((char *)&DAT_005716a0 + offset);
  }
  else
  {
    FUN_002663a0(0x8FE);
    DAT_00355064 |= 0x2000;
  }
}

// Preserve original symbol
void FUN_00239178(void)
{
  text_op_00_load_palette_entry_or_advance();
}
