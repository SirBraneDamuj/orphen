/*
 * Opcode 0xDD — audio_dispatch_with_two_params
 *
 * Original: FUN_00264f18
 *
 * Args: expr `a`, expr `b`. Calls audio_dispatch(-1, b, a) — passes both
 * parameter words as the "channel"/"value" pair while letting the audio
 * subsystem pick the id.
 */

extern void script_eval_expression(void *out);
extern void audio_dispatch(unsigned long id, unsigned int b, unsigned int a);

int op_0xDD_audio_dispatch_with_two_params(void) {
    unsigned int a, b;
    script_eval_expression(&a);
    script_eval_expression(&b);
    audio_dispatch(0xFFFFFFFFFFFFFFFFul, b, a);
    return 0;
}
