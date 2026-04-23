/*
 * Opcode 0x131 — set_audio_byte_3555d6
 *
 * Original: FUN_002616b8
 *
 * Same shape as 0x130 — diagnostic threshold 0x80, message at 0x34d0b0,
 * stores low byte into DAT_003555d6.
 */

extern unsigned char DAT_003555d6;
extern void          script_eval_expression(void *out);
extern void          script_diagnostic(unsigned int msg_addr);

unsigned int op_0x131_set_audio_byte_3555d6(void) {
    unsigned int v[4];
    script_eval_expression(v);
    if (v[0] > 0x80) script_diagnostic(0x34d0b0);
    DAT_003555d6 = (unsigned char)v[0];
    return 0;
}
