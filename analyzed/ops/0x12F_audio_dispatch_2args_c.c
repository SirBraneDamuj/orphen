/*
 * Opcode 0x12F — audio_dispatch_2args_c
 *
 * Original: FUN_00261638
 *
 * Reads two exprs and forwards to FUN_002060a8.
 */

extern void script_eval_expression(void *out);
extern void FUN_002060a8(unsigned int a, unsigned int b);

unsigned int op_0x12F_audio_dispatch_2args_c(void)
{
  unsigned int a, b;
  script_eval_expression(&a);
  script_eval_expression(&b);
  FUN_002060a8(a, b);
  return 0;
}
