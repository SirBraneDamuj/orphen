/*
 * Opcode 0xCD — sample_motion_keyframe_into_entity
 *
 * Original: FUN_00264628
 *
 * Args: expr `selector`. Selects the entity. If the selected entity's
 * type word is < 0x7C, sets `mode = 1`, else `mode = 0`; values > 0x1F0
 * raise diag 0x34d350.
 *
 * Calls keyframe_sample (FUN_0025bae8, mode, type, scratch_buf) which
 * fills a scratch with two ints (translate XY at +0x10/+0x14) and three
 * signed bytes (rotation indices at +0x6/+0x7/+0x8). Writes them into
 * the entity:
 *   +0x54 / +0x11C  ← translate X
 *   +0x58 / +0x120  ← translate Y
 *   +0x128 / +0x12A ← rotate index 0 (broadcast as short)
 *   +0x12C ← rotate index 1
 *   +0x12E ← rotate index 2
 */

#include <stdint.h>

extern short *DAT_00355044; /* selected_entity */
extern void script_eval_expression(void *out);
extern void script_select_entity(int i, void *prev);
extern void script_diagnostic(unsigned int);
extern void keyframe_sample(unsigned long mode, short type, void *out_buf);

int op_0xCD_sample_motion_keyframe_into_entity(void)
{
  short *prev = DAT_00355044;
  unsigned long mode = 0;
  int selector[4];
  struct
  {
    unsigned char pad[6];
    signed char r0;
    signed char r1;
    signed char r2;
    unsigned char pad2[7];
    unsigned int tx;
    unsigned int ty;
  } buf;
  script_eval_expression(selector);
  script_select_entity(selector[0], prev);

  if (*DAT_00355044 < 0x7C)
    mode = 1;
  else if (*DAT_00355044 > 0x1F0)
    script_diagnostic(0x34d350);

  keyframe_sample(mode, *DAT_00355044, &buf);

  short *e = DAT_00355044;
  *(unsigned int *)(e + 0x2A) = buf.tx; /* +0x54 */
  *(unsigned int *)(e + 0x8E) = buf.tx; /* +0x11C */
  *(unsigned int *)(e + 0x2C) = buf.ty; /* +0x58 */
  *(unsigned int *)(e + 0x90) = buf.ty; /* +0x120 */
  e[0x95] = (short)buf.r0;              /* +0x12A */
  e[0x94] = (short)buf.r0;              /* +0x128 */
  e[0x96] = (short)buf.r1;              /* +0x12C */
  e[0x97] = (short)buf.r2;              /* +0x12E */
  return 0;
}
