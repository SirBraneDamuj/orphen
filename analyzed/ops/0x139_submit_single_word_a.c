/*
 * Opcode 0x139 — submit_single_word_a
 *
 * Original: FUN_00265350
 *
 * Reads one expr and forwards it to FUN_00202f88.
 */

extern void script_eval_expression(void *out);
extern void FUN_00202f88(unsigned int v);

unsigned int op_0x139_submit_single_word_a(void) {
    unsigned int v[4];
    script_eval_expression(v);
    FUN_00202f88(v[0]);
    return 0;
}
