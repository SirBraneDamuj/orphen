/*
 * Opcodes 0x119 / 0x11A / 0x11B — entity_set_pair_b (shared FUN_00264a60)
 *
 * Same shape as 0x116/7/8 but forwards to FUN_002257e0 instead.
 */

extern short          sGpffffbd68;
extern int            iGpffffb0d4;
extern unsigned int   uGpffffb788;
extern unsigned int   DAT_00345a2c;
extern void           script_eval_expression(void *out);
extern void           script_select_entity(unsigned int idx, int ctx);
extern void           FUN_002257e0(unsigned int target,
                                   unsigned int p0, unsigned int p1);

unsigned int op_0x119_0x11A_0x11B_entity_set_pair_b(void) {
    short        opcode = sGpffffbd68;
    int          ctx    = iGpffffb0d4;
    unsigned int idx;
    unsigned int p0, p1;
    if (opcode == 0x119) {
        script_eval_expression(&idx);
        script_eval_expression(&p0);
        script_eval_expression(&p1);
        script_select_entity(idx, ctx);
        FUN_002257e0(*(unsigned int *)(intptr_t)(iGpffffb0d4 + 0x164), p0, p1);
    } else {
        script_eval_expression(&p0);
        script_eval_expression(&p1);
        if (opcode == 0x11A) FUN_002257e0(uGpffffb788, p0, p1);
        else                 FUN_002257e0(DAT_00345a2c, p0, p1);
    }
    return 0;
}
