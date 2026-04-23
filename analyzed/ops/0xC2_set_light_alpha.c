/*
 * Opcode 0xC2 — set_light_alpha
 *
 * Original: FUN_00264148
 *
 * Args: expr `slot`, expr `alpha_byte`. Writes one byte
 *   light_table[slot].alpha (&DAT_00343897 + slot*0x14) = alpha_byte.
 */

extern unsigned char DAT_00343897[];
extern void script_eval_expression(void *out);

int op_0xC2_set_light_alpha(void)
{
  int slot;
  unsigned char alpha;
  script_eval_expression(&slot);
  script_eval_expression(&alpha);
  DAT_00343897[slot * 0x14] = alpha;
  return 0;
}
