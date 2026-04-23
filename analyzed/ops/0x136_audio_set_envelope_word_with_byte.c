/*
 * Opcode 0x136 — audio_set_envelope_word_with_byte
 *
 * Original: FUN_00261808
 *
 * Reads two exprs (envelope_word, byte_param). Computes
 * `byte_param mod 256` (signed-safe) and stores the result into
 * uGpffffb667, then forwards envelope_word to FUN_00206d98.
 */

extern unsigned char uGpffffb667;
extern void          script_eval_expression(void *out);
extern void          FUN_00206d98(unsigned int v);

unsigned int op_0x136_audio_set_envelope_word_with_byte(void) {
    unsigned int env;
    int          byte_param;
    script_eval_expression(&env);
    script_eval_expression(&byte_param);
    int t = (byte_param < 0) ? (byte_param + 0xFF) : byte_param;
    byte_param -= (t >> 8) << 8;
    uGpffffb667 = (unsigned char)byte_param;
    FUN_00206d98(env);
    return 0;
}
