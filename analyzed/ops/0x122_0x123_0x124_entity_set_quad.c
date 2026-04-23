/*
 * Opcodes 0x122 / 0x123 / 0x124 — entity_set_quad (shared FUN_00264c88)
 *
 * Same target-selection pattern as 0x11F/120/121 (0x122 = expr+pool,
 * 0x123 = uGpffffb788, 0x124 = DAT_00345a2c). Reads 3 trailing params
 * and forwards to FUN_00225820(target, p0, p1, p2).
 */

extern short          sGpffffbd68;
extern int            iGpffffb0d4;
extern unsigned int   uGpffffb788;
extern unsigned int   DAT_00345a2c;
extern void           script_eval_expression(void *out);
extern void           script_select_entity(unsigned int idx, int ctx);
extern void           FUN_00225820(unsigned int target,
                                   unsigned int p0, unsigned int p1,
                                   unsigned int p2);

unsigned int op_0x122_0x123_0x124_entity_set_quad(void) {
    int          ctx    = iGpffffb0d4;
    unsigned int target = uGpffffb788;
    unsigned int idx, p0, p1, p2;
    if (sGpffffbd68 != 0x123) {
        if (sGpffffbd68 < 0x124) {
            target = 0;
            if (sGpffffbd68 == 0x122) {
                script_eval_expression(&idx);
                script_select_entity(idx, ctx);
                target = *(unsigned int *)(intptr_t)(iGpffffb0d4 + 0x164);
            }
        } else {
            target = (sGpffffbd68 == 0x124) ? DAT_00345a2c : 0;
        }
    }
    script_eval_expression(&p0);
    script_eval_expression(&p1);
    script_eval_expression(&p2);
    FUN_00225820(target, p0, p1, p2);
    return 0;
}
