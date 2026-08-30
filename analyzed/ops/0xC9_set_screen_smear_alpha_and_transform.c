/*
 * Opcode 0xC9 — set_screen_smear_alpha_and_transform
 *
 * Original: FUN_00264470 (0x00264470)
 *
 * 0xC8 plus the shape of the smear. Reads *six* expressions, not seven: the
 * alpha, then five more that land in the contiguous short array at
 * DAT_00343878. Ghidra's stack layout hides the sixth behind a second local
 * (`auStack_30`), which is really `auStack_40 + 0x10` — the fifth element of
 * the same array — so the earlier reading of this as "five colours plus one
 * ignored value" was counting the same slot twice.
 *
 *   expr 0 -> DAT_00355661   blend alpha (low byte; see 0xC8)
 *   expr 1 -> DAT_00343878   destination offset X, in 1/16 pixel
 *   expr 2 -> DAT_0034387A   destination offset Y, in 1/16 line (halved on use)
 *   expr 3 -> DAT_0034387C   destination scale X, as (v + 1024) / 1024
 *   expr 4 -> DAT_0034387E   destination scale Y, same form
 *   expr 5 -> DAT_00343880   destination rotation, in tenths of a degree
 *
 * NOT a palette. This file previously called the five shorts a colour table.
 * Their only reader is FUN_00201a38, which uses them to transform the four
 * corners of the feedback quad — see analyzed/frame_feedback_blit.c. The
 * source rectangle is never transformed, so the effect stretches the picture
 * rather than panning across it: scale gives a zoom blur, rotation a swirl,
 * offset a directional smear, and all zero a plain ghost.
 *
 * Scale is signed and biased at 1024, so 0x32 (50) is a 4.9% zoom in and
 * -70 is a 6.8% zoom out. FUN_0023c340 — the battle/spell path, which writes
 * these globals directly rather than through the VM — uses 0x32, 0x78 and -70
 * with alphas of 0x50 and (counter + 'Z'), which is the impact punch.
 */

extern unsigned char DAT_00355661;    /* bGpffffb6f1 — frame-feedback target alpha */
extern unsigned short DAT_00343878[]; /* offsetX, offsetY, scaleX, scaleY, rotation */
extern void bytecode_interpreter(void *out); /* orig FUN_0025c258 */

unsigned int opcode_0xC9_set_screen_smear_alpha_and_transform(void)
{
  unsigned char alpha[16];
  /* The original evaluates into one 16-byte slot and its +4/+8/+0xC/+0x10
   * sub-slots, then reads back every fourth byte as a short. */
  unsigned short transform[10];
  int index;

  bytecode_interpreter(alpha);
  bytecode_interpreter(&transform[0]);
  bytecode_interpreter(&transform[2]);
  bytecode_interpreter(&transform[4]);
  bytecode_interpreter(&transform[6]);
  bytecode_interpreter(&transform[8]);

  DAT_00355661 = alpha[0];
  for (index = 0; index < 5; index++)
  {
    DAT_00343878[index] = transform[index * 2];
  }
  return 0;
}
