/*
 * Opcode 0x12B — audio_dispatch_3args_b
 *
 * Original: FUN_00261570
 *
 * Reads three raw exprs and forwards to FUN_00206260.
 */

extern void script_eval_expression(void *out);
extern void FUN_00206260(unsigned int a, unsigned int b, unsigned int c);

unsigned int op_0x12B_audio_dispatch_3args_b(void) {
    unsigned int a, b, c;
    script_eval_expression(&a);
    script_eval_expression(&b);
    script_eval_expression(&c);
    FUN_00206260(a, b, c);
    return 0;
}
