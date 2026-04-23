/*
 * Opcode 0xDC — audio_dispatch_with_id
 *
 * Original: FUN_00264ee8
 *
 * Reads expr `id` and calls audio_dispatch(id, -1, 0) (FUN_0023baf8).
 */

extern void script_eval_expression(void *out);
extern void audio_dispatch(unsigned int id, long a, int b); /* FUN_0023baf8 */

int op_0xDC_audio_dispatch_with_id(void) {
    unsigned int args[4];
    script_eval_expression(args);
    audio_dispatch(args[0], -1, 0);
    return 0;
}
