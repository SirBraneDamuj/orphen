/*
 * Opcode 0x12C — audio_dispatch_1arg_a
 *
 * Original: FUN_002615b0
 *
 * Reads one expr and forwards to FUN_00206238.
 */

extern void script_eval_expression(void *out);
extern void FUN_00206238(unsigned int a);

void op_0x12C_audio_dispatch_1arg_a(void) {
    unsigned int v[4];
    script_eval_expression(v);
    FUN_00206238(v[0]);
}
