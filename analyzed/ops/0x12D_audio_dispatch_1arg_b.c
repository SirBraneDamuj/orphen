/*
 * Opcode 0x12D — audio_dispatch_1arg_b
 *
 * Original: FUN_002615d8
 *
 * Reads one expr and forwards to FUN_00205f40.
 */

extern void script_eval_expression(void *out);
extern void FUN_00205f40(unsigned int a);

unsigned int op_0x12D_audio_dispatch_1arg_b(void) {
    unsigned int v[4];
    script_eval_expression(v);
    FUN_00205f40(v[0]);
    return 0;
}
