/*
 * Opcode 0xCE — select_entity_debug_hook
 *
 * Original: FUN_00264700
 *
 * Args: expr `selector`. Selects the entity (0x100 keeps the one selected
 * before the operand was evaluated) and passes it to FUN_0026bf38.
 *
 * FUN_0026bf38 is `jr ra; nop` in the retail ELF -- an empty hook, most
 * likely a debug call with its body compiled out. The earlier guess of a
 * "reset/apply default state" was wrong: the only effect is the selection.
 * Returns 0.
 */

extern void *DAT_00355044;            /* selected_entity */
extern void  script_eval_expression(void *out);
extern void  script_select_entity(int i, void *prev);
extern void  FUN_0026bf38(void *entity); /* empty */

int op_0xCE_select_entity_debug_hook(void) {
    void *prev = DAT_00355044;
    int   selector[4];
    script_eval_expression(selector);
    script_select_entity(selector[0], prev);
    FUN_0026bf38(DAT_00355044);
    return 0;
}
