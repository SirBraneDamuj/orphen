/*
 * Opcode 0x115 — submit_simple_byte_param
 *
 * Original: FUN_00262f10
 *
 * Reads one expression and forwards its low byte to FUN_002218f0.
 */

extern void script_eval_expression(void *out);
extern void FUN_002218f0(unsigned char param);

unsigned int op_0x115_submit_simple_byte_param(void)
{
  unsigned char v[16];
  script_eval_expression(v);
  FUN_002218f0(v[0]);
  return 0;
}
