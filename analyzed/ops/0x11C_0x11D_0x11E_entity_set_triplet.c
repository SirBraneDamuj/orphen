/*
 * Opcodes 0x11C / 0x11D / 0x11E — entity_set_triplet (shared FUN_00264b18)
 *
 * Same shape as 0x116/7/8 but reads 3 trailing params and forwards to
 * FUN_00225910.
 */

extern short          sGpffffbd68;
extern int            iGpffffb0d4;
extern unsigned int   uGpffffb788;
extern unsigned int   DAT_00345a2c;
extern void           script_eval_expression(void *out);
extern void           script_select_entity(unsigned int idx, int ctx);
extern void           FUN_00225910(unsigned int target,
                                   unsigned int p0, unsigned int p1,
                                   unsigned int p2);

unsigned int op_0x11C_0x11D_0x11E_entity_set_triplet(void) {
    short        opcode = sGpffffbd68;
    int          ctx    = iGpffffb0d4;
    unsigned int idx;
    unsigned int p0, p1, p2;
    if (opcode == 0x11C) {
        script_eval_expression(&idx);
        script_eval_expression(&p0);
        script_eval_expression(&p1);
        script_eval_expression(&p2);
        script_select_entity(idx, ctx);
        FUN_00225910(*(unsigned int *)(intptr_t)(iGpffffb0d4 + 0x164), p0, p1, p2);
    } else {
        script_eval_expression(&p0);
        script_eval_expression(&p1);
        script_eval_expression(&p2);
        if (opcode == 0x11D) FUN_00225910(uGpffffb788, p0, p1, p2);
        else                 FUN_00225910(DAT_00345a2c, p0, p1, p2);
    }
    return 0;
}
