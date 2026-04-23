/*
 * Opcodes 0xA4 / 0xA6 — audio_submit (shared handler)
 *
 * Original: FUN_00261f60 (dispatched for both opcode ids)
 *
 * Reads one expression (parameter) and one inline byte from the script.
 * Branches on `current_opcode_id` (sGpffffbd68):
 *   0xA4: audio_submit_type_A(expr, byte, 0x20)   (FUN_0022dbc8)
 *   0xA6: audio_submit_type_B(expr, byte, 0x800)  (FUN_0022dc68)
 * Other opcode ids fall through with no submission.
 */

extern unsigned char *script_byte_cursor;     /* puGpffffbd60 */
extern short          current_opcode_id;      /* sGpffffbd68 */
extern void           script_eval_expression(void *out);                    /* FUN_0025c258 */
extern void           audio_submit_type_A(int p, unsigned char b, int f);   /* FUN_0022dbc8 */
extern void           audio_submit_type_B(int p, unsigned char b, int f);   /* FUN_0022dc68 */

void op_0xA4_0xA6_audio_submit(void) {
    short op = current_opcode_id;
    int   args[4];
    script_eval_expression(args);
    unsigned char inline_byte = *script_byte_cursor++;
    if (op == 0xA4) {
        audio_submit_type_A(args[0], inline_byte, 0x20);
    } else if (op == 0xA6) {
        audio_submit_type_B(args[0], inline_byte, 0x800);
    }
}
