/*
 * Opcode 0x146 — submit_single_word_b
 *
 * Original: FUN_00265738
 *
 * Reads one expr and forwards it to FUN_00213640.
 */

extern void script_eval_expression(void *out);
extern void FUN_00213640(unsigned int v);

unsigned int op_0x146_submit_single_word_b(void) {
    unsigned int v[4];
    script_eval_expression(v);
    FUN_00213640(v[0]);
    return 0;
}
