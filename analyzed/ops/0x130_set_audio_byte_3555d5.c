/*
 * Opcode 0x130 — set_audio_byte_3555d5
 *
 * Original: FUN_00261670
 *
 * Reads one expression. If > 0x80 emits a script_diagnostic message
 * (string at 0x34d088, "audio bank index out of range"). Stores the
 * low byte into DAT_003555d5.
 */

extern unsigned char DAT_003555d5;
extern void          script_eval_expression(void *out);
extern void          script_diagnostic(unsigned int msg_addr);   /* FUN_0026bfc0 */

unsigned int op_0x130_set_audio_byte_3555d5(void) {
    unsigned int v[4];
    script_eval_expression(v);
    if (v[0] > 0x80) script_diagnostic(0x34d088);
    DAT_003555d5 = (unsigned char)v[0];
    return 0;
}
