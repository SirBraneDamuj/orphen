/*
 * Opcode 0x5B FUN_0025f1c8 — read field from script slot by index
 *
 * Reads one expression (slot index, bounded by stride 0x10) and one byte
 * field selector. The per-slot structure lives at DAT_003556e8 + idx*0x10:
 *   - field < 4 → return raw byte at slot[+0xC + field] (flags/tags)
 *   - field ≥ 4 → return FUN_0030bd20(float_at(slot[+((field-4)*4)]) * scale)
 *                 where scale = DAT_00352bc4 (packs float → fixed for script).
 *
 * Pairs with the 0x4E lookup/push opcode that populates these slots.
 */

extern void FUN_0025c258(void *);
extern unsigned short FUN_0030bd20(float);
extern unsigned char *DAT_00355cd0;
extern int DAT_003556e8;
extern float DAT_00352bc4;

unsigned int op_0x5B_read_slot_field(void)
{
  unsigned char idx[16];
  FUN_0025c258(idx);
  unsigned char field = *DAT_00355cd0++;
  int entry = DAT_003556e8 + (unsigned)idx[0] * 0x10;
  if (field < 4)
    return *(unsigned char *)(entry + 0xC + field);
  return FUN_0030bd20(*(float *)(entry + (field - 4) * 4) * DAT_00352bc4);
}
