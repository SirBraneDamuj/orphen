/*
 * Opcode 0x12A — audio_dispatch_3args_a
 *
 * Original: FUN_00261530
 *
 * Reads three raw exprs and forwards to FUN_002063c8.
 */

extern void script_eval_expression(void *out);
extern void FUN_002063c8(unsigned int a, unsigned int b, unsigned int c);

unsigned int op_0x12A_audio_dispatch_3args_a(void)
{
  unsigned int a, b, c;
  script_eval_expression(&a);
  script_eval_expression(&b);
  script_eval_expression(&c);
  FUN_002063c8(a, b, c);
  return 0;
}
