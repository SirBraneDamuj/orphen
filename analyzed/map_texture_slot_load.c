/*
 * FUN_0022a178 / FUN_0022a1f8 -- the map's texture pages into the global slots.
 *
 * Original: FUN_0022a178 @ 0x0022a178, FUN_0022a1f8 @ 0x0022a1f8
 *
 * Behaviour
 *   FUN_0022a178 walks a ten-entry u16 table at DAT_00325394 and loads every
 *   non-zero entry into texture slot `i` (i = 0..9), then records the id it
 *   loaded in a parallel table at DAT_00325350. FUN_0022a1f8 clears both.
 *
 * Why this matters
 *   DAT_003429a8 is **one global array of texture slots**, not a per-consumer
 *   set. The banks partition it by convention only:
 *
 *     slots  0..9    the current map's texture pages (this function)
 *     slots 10..23   FUN_00266118's default entity bank
 *     slots 24..39   FUN_00266118's alternate entity bank
 *     slots 32..48   FUN_00221fd8's fixed boot binds (font, chest, item icons)
 *
 *   So two things that look like private page indices are really indices into
 *   this one array:
 *
 *     - A PSM2 material slot's `type` byte. FUN_00211230:186 emits
 *       `type + 1` as packet byte 6.
 *     - A PSC3 subdraw's texFlags bits 10..7. FUN_00212058:180-208 emits the
 *       selector itself as packet byte 6 when the owning primitive's flag
 *       0x800 is clear -- the same encoding -- so selector N names global slot
 *       N-1, *not* the entity's own bound slot. Selector 0 emits 0x3F, which
 *       is "the entity's bound slot"; selector 0xF emits 0x3E with byte 5 =
 *       0x11, the same special mode FUN_00211230 reaches through its type 9.
 *
 *   That is how a lantern model bound to the shop's sheet still draws its
 *   flame out of the map's effect page, and how the window curtains draw out
 *   of the map's page 3 while bound to slot 22.
 *
 * Who fills DAT_00325394
 *   Not yet traced. The scene/map loader stages it before calling this; the
 *   entries are the BMPA records that follow the map's PSM2 record in the MCB
 *   bundle, in bundle order.
 *
 * Ground truth (eeMemory.bin, s01_e012)
 *   DAT_00325394 = {0286, 0002, 02c8, 0253, 000d, 02c9, 000f, 0, 0, 0}
 *   DAT_00325350 = {0286, 0002, 02c8, 0253, 000d, 02c9, 000f, 0244, 0030, 0002}
 *   DAT_003429a8[0..6] = the same seven ids in the same order.
 *
 *   Slots 7..9 in DAT_00325350 and DAT_003429a8 are stale entries from a
 *   previous map: this loop only writes where the source table is non-zero, so
 *   a shorter page list leaves the tail of the previous one behind.
 *
 * PS2 notes
 *   FUN_002102e8(slot, id) is FUN_00210218(3, id) -- category 3 is TEX.BIN --
 *   followed by the GS upload in FUN_00210368, then the residency write. It is
 *   FUN_00210280 with the category fixed and the two writes in the other
 *   order.
 */

extern short DAT_00325394[10]; /* pending: the map's texture ids, staged by the loader */
extern short DAT_00325350[10]; /* resident: what this function last loaded      */

void FUN_002102e8(int slot, short textureId);

/* FUN_0022a178 */
void map_texture_slots_load(void)
{
  short *resident = DAT_00325350;
  short *pending = DAT_00325394;
  int slot = 0;

  do
  {
    int next = slot + 1;
    if (*pending != 0)
    {
      FUN_002102e8(slot, *pending);
      *resident = *pending;
    }
    ++resident;
    ++pending;
    slot = next;
  } while (next < 10);
}

/* FUN_0022a1f8 */
void map_texture_slots_clear(void)
{
  short *resident = DAT_00325350;
  short *pending = DAT_00325394;
  int remaining = 9;

  do
  {
    *resident = 0;
    --remaining;
    *pending = 0;
    ++resident;
    ++pending;
  } while (-1 < remaining);
}
