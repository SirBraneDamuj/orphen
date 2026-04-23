/*
 * Opcode 0x149 — set_global_byte_355656
 *
 * Original: FUN_00265790
 *
 * Reads one expr; stores low byte to DAT_00355656.
 */

extern unsigned char DAT_00355656;
extern void script_eval_expression(void *out);

unsigned int op_0x149_set_global_byte_355656(void)
{
  unsigned char v[16];
  script_eval_expression(v);
  DAT_00355656 = v[0];
  return 0;
}
