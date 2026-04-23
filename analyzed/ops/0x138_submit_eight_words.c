/*
 * Opcode 0x138 — submit_eight_words
 *
 * Original: FUN_002652d0
 *
 * Reads 8 raw exprs and forwards them to FUN_00202f00.
 */

extern void script_eval_expression(void *out);
extern void FUN_00202f00(unsigned int, unsigned int, unsigned int,
                         unsigned int, unsigned int, unsigned int,
                         unsigned int, unsigned int);

unsigned int op_0x138_submit_eight_words(void) {
    unsigned int a[8];
    for (int i = 0; i < 8; i++) script_eval_expression(&a[i]);
    FUN_00202f00(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    return 0;
}
