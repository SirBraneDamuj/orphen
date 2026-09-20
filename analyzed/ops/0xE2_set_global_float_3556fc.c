/*
 * Opcode 0xE2 - set_effect_water_line
 *
 * Original: FUN_002650e0
 *
 * Reads one int expression and stores it / DAT_00352cd0 (100000.0, the same
 * divisor every coordinate operand uses) into DAT_003556fc.
 *
 * CORRECTED 2026-09-19. The old note called the destination "a tunable
 * timing/scale float" and left it there. It is a **world z**: Ghidra spells the
 * same word `fGpffffb78c` inside FUN_002ED3E0 and FUN_002ED9A0, the burning
 * ship's fire and smoke, and both put every entity they spawn at that height,
 * with its +0x4C and +0x50 ground fields set to it as well. It is the water
 * line the wreck burns on.
 *
 * s01_e013's monster animatic is the one scene in the slice that writes it, at
 * script offset 0x83A, and it writes zero -- the animatic's sea level.
 *
 * See analyzed/actor_behaviors/type_0x1AA_ship_fire.c.
 */

extern float DAT_003556fc;   /* fGpffffb78c: the effect water line */
extern float DAT_00352cd0;   /* 100000.0 */
extern void  script_eval_expression(void *out);

int op_0xE2_set_effect_water_line(void) {
    int args[4];
    script_eval_expression(args);
    DAT_003556fc = (float)args[0] / DAT_00352cd0;
    return 0;
}
