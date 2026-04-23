/*
 * Opcode 0x113 — set_global_uGpffffad54
 *
 * Original: FUN_00262db0
 *
 * Reads one expression and stores it into uGpffffad54.
 */

extern unsigned int uGpffffad54;
extern void script_eval_expression(void *out);

unsigned int op_0x113_set_global_uGpffffad54(void) {
    unsigned int v[4];
    script_eval_expression(v);
    uGpffffad54 = v[0];
    return 0;
}
