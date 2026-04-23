/*
 * Opcode 0xC8 — set_text_color_index
 *
 * Original: FUN_00264448
 *
 * Reads one expression and stores its low byte into DAT_00355661 (the
 * active text colour palette index used by the dialogue/menu renderer).
 */

extern unsigned char DAT_00355661;
extern void script_eval_expression(void *out);

int op_0xC8_set_text_color_index(void)
{
  unsigned char buf[16];
  script_eval_expression(buf);
  DAT_00355661 = buf[0];
  return 0;
}
