/*
 * Opcode 0x129 — audio_dispatch_2args
 *
 * Original: FUN_00261500
 *
 * Reads two raw exprs and forwards to FUN_00205d90.
 */

extern void script_eval_expression(void *out);
extern void FUN_00205d90(unsigned int a, unsigned int b);

void op_0x129_audio_dispatch_2args(void) {
    unsigned int a, b;
    script_eval_expression(&a);
    script_eval_expression(&b);
    FUN_00205d90(a, b);
}
