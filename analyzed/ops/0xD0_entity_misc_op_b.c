/*
 * Opcode 0xD0 — entity_misc_op_b
 *
 * Original: FUN_00264790
 *
 * Args: expr `selector`. Selects the entity and forwards to FUN_00215e48
 * — un-analyzed callee.
 */

extern void *DAT_00355044;
extern void  script_eval_expression(void *out);
extern void  script_select_entity(int i, void *prev);
extern void  FUN_00215e48(void *entity);

int op_0xD0_entity_misc_op_b(void) {
    void *prev = DAT_00355044;
    int   selector[4];
    script_eval_expression(selector);
    script_select_entity(selector[0], prev);
    FUN_00215e48(DAT_00355044);
    return 0;
}
