/*
 * Opcode 0x13A — set_global_byte_3555d2
 *
 * Original: FUN_00265378
 *
 * Reads one expr and stores the low byte into DAT_003555d2.
 */

extern unsigned char DAT_003555d2;
extern void          script_eval_expression(void *out);

unsigned int op_0x13A_set_global_byte_3555d2(void) {
    unsigned char v[16];
    script_eval_expression(v);
    DAT_003555d2 = v[0];
    return 0;
}
