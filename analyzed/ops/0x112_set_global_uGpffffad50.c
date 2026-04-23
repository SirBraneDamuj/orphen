/*
 * Opcode 0x112 — set_global_uGpffffad50
 *
 * Original: FUN_00262d88
 *
 * Reads one expression and stores it into uGpffffad50.
 */

extern unsigned int uGpffffad50;
extern void script_eval_expression(void *out);

unsigned int op_0x112_set_global_uGpffffad50(void) {
    unsigned int v[4];
    script_eval_expression(v);
    uGpffffad50 = v[0];
    return 0;
}
