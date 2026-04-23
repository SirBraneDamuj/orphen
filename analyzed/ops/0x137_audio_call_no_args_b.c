/*
 * Opcode 0x137 — audio_call_no_args_b
 *
 * Original: FUN_00261868
 *
 * Reads one expression (its value is discarded) and calls FUN_00206a90.
 */

extern void script_eval_expression(void *out);
extern void FUN_00206a90(void);

void op_0x137_audio_call_no_args_b(void)
{
  unsigned char ignored[16];
  script_eval_expression(ignored);
  FUN_00206a90();
}
