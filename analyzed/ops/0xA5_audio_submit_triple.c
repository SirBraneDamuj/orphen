/*
 * Opcode 0xA5 — audio_submit_triple
 *
 * Original: FUN_00262058
 *
 * Reads one expression, one inline byte from the script IP, then a second
 * expression, and forwards them to audio_submit_three (FUN_0022db50).
 */

extern unsigned char *script_byte_cursor;             /* DAT_00355cd0 */
extern void           script_eval_expression(void *out); /* FUN_0025c258 */
extern void           audio_submit_three(int a, unsigned char b, int c); /* FUN_0022db50 */

int op_0xA5_audio_submit_triple(void) {
    int           expr_a;
    int           expr_c;
    script_eval_expression(&expr_a);
    unsigned char inline_byte = *script_byte_cursor++;
    script_eval_expression(&expr_c);
    audio_submit_three(expr_a, inline_byte, expr_c);
    return 0;
}
