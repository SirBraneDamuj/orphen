/*
 * Opcode 0xE3 — set_global_byte_355641
 *
 * Original: FUN_00265120
 *
 * Reads one expression and stores its low byte into DAT_00355641.
 */

extern unsigned char DAT_00355641;
extern void          script_eval_expression(void *out);

int op_0xE3_set_global_byte_355641(void) {
    unsigned char buf[16];
    script_eval_expression(buf);
    DAT_00355641 = buf[0];
    return 0;
}
