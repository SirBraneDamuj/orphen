/*
 * Opcodes 0x116 / 0x117 / 0x118 — entity_set_pair_a (shared FUN_002649a8)
 *
 * For 0x116: read entity index + 2 params, select that entity, then
 *   FUN_002257c0(entity_field_0x164, p0, p1).
 * For 0x117: skip the entity index expression and use uGpffffb788.
 * For 0x118 (default): use DAT_00345a2c.
 */

extern short          sGpffffbd68;
extern int            iGpffffb0d4;        /* entity context register */
extern unsigned int   uGpffffb788;
extern unsigned int   DAT_00345a2c;
extern void           script_eval_expression(void *out);
extern void           script_select_entity(unsigned int idx, int ctx);   /* FUN_0025d6c0 */
extern void           FUN_002257c0(unsigned int target,
                                   unsigned int p0, unsigned int p1);

unsigned int op_0x116_0x117_0x118_entity_set_pair_a(void) {
    short        opcode = sGpffffbd68;
    int          ctx    = iGpffffb0d4;
    unsigned int idx;
    unsigned int p0, p1;
    if (opcode == 0x116) {
        script_eval_expression(&idx);
        script_eval_expression(&p0);
        script_eval_expression(&p1);
        script_select_entity(idx, ctx);
        FUN_002257c0(*(unsigned int *)(intptr_t)(iGpffffb0d4 + 0x164), p0, p1);
    } else {
        script_eval_expression(&p0);
        script_eval_expression(&p1);
        if (opcode == 0x117) FUN_002257c0(uGpffffb788, p0, p1);
        else                 FUN_002257c0(DAT_00345a2c, p0, p1);
    }
    return 0;
}
