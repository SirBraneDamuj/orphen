/*
 * Opcode 0xCA — set_dialogue_speaker_byte
 *
 * Original: FUN_00264500
 *
 * Reads one expression and stores its low byte into DAT_00355700 (the
 * dialogue speaker / portrait id byte).
 */

extern unsigned char DAT_00355700;
extern void          script_eval_expression(void *out);

int op_0xCA_set_dialogue_speaker_byte(void) {
    unsigned char buf[16];
    script_eval_expression(buf);
    DAT_00355700 = buf[0];
    return 0;
}
