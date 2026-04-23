/*
 * Opcode 0xDA — set_global_byte_b6f0
 *
 * Original: FUN_00264ea0
 *
 * Reads one expression and stores its low byte into uGpffffb6f0.
 */

extern unsigned char uGpffffb6f0;
extern void script_eval_expression(void *out);

int op_0xDA_set_global_byte_b6f0(void)
{
  unsigned char buf[16];
  script_eval_expression(buf);
  uGpffffb6f0 = buf[0];
  return 0;
}
