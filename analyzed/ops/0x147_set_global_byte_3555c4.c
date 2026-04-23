/*
 * Opcode 0x147 — set_global_byte_3555c4
 *
 * Original: FUN_00265760
 *
 * Reads one expr; stores low byte to DAT_003555c4.
 */

extern unsigned char DAT_003555c4;
extern void          script_eval_expression(void *out);

unsigned int op_0x147_set_global_byte_3555c4(void) {
    unsigned char v[16];
    script_eval_expression(v);
    DAT_003555c4 = v[0];
    return 0;
}
