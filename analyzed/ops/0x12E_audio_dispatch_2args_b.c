/*
 * Opcode 0x12E — audio_dispatch_2args_b
 *
 * Original: FUN_00261600
 *
 * Reads two exprs and forwards to FUN_00205f98.
 */

extern void script_eval_expression(void *out);
extern void FUN_00205f98(unsigned int a, unsigned int b);

unsigned int op_0x12E_audio_dispatch_2args_b(void) {
    unsigned int a, b;
    script_eval_expression(&a);
    script_eval_expression(&b);
    FUN_00205f98(a, b);
    return 0;
}
