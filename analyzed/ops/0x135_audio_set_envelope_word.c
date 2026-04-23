/*
 * Opcode 0x135 — audio_set_envelope_word
 *
 * Original: FUN_002617e0
 *
 * Reads one expr and forwards to FUN_00206d98.
 */

extern void script_eval_expression(void *out);
extern void FUN_00206d98(unsigned int v);

unsigned int op_0x135_audio_set_envelope_word(void) {
    unsigned int v[4];
    script_eval_expression(v);
    FUN_00206d98(v[0]);
    return 0;
}
