/*
 * Opcode 0xE5 — emit_audio_pair_with_inline_byte
 *
 * Original: FUN_002651a0
 *
 * Reads one inline byte (`type`) from script IP, then one expression
 * `param`. Forwards to FUN_0022cd88(type, param) — appears to push a
 * two-word audio command to the mixer.
 */

extern unsigned short *DAT_00355cd0;             /* script_byte_cursor (read as u16) */
extern void script_eval_expression(void *out);
extern void FUN_0022cd88(unsigned short type, unsigned int param);

int op_0xE5_emit_audio_pair_with_inline_byte(void) {
    unsigned short type = *DAT_00355cd0;
    DAT_00355cd0 = (unsigned short *)((char *)DAT_00355cd0 + 1);
    unsigned int param[4];
    script_eval_expression(param);
    FUN_0022cd88(type, param[0]);
    return 0;
}
