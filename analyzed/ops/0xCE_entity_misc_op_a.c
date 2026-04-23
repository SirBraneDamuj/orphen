/*
 * Opcode 0xCE — entity_misc_op_a
 *
 * Original: FUN_00264700
 *
 * Args: expr `selector`. Selects the entity and forwards it to
 * FUN_0026bf38 — currently un-analyzed, but the call shape (pointer to
 * the selected entity) suggests an "apply default state / reset" type
 * operation. Returns 0.
 */

extern void *DAT_00355044;            /* selected_entity */
extern void  script_eval_expression(void *out);
extern void  script_select_entity(int i, void *prev);
extern void  FUN_0026bf38(void *entity);

int op_0xCE_entity_misc_op_a(void) {
    void *prev = DAT_00355044;
    int   selector[4];
    script_eval_expression(selector);
    script_select_entity(selector[0], prev);
    FUN_0026bf38(DAT_00355044);
    return 0;
}
