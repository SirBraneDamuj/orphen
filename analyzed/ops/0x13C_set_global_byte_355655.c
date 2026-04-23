/*
 * Opcode 0x13C — set_global_byte_355655
 *
 * Original: FUN_002653e8
 *
 * Reads one expr and stores the low byte into DAT_00355655.
 */

extern unsigned char DAT_00355655;
extern void script_eval_expression(void *out);

unsigned int op_0x13C_set_global_byte_355655(void)
{
  unsigned char v[16];
  script_eval_expression(v);
  DAT_00355655 = v[0];
  return 0;
}
