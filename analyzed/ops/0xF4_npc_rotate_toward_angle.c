/*
 * Opcode 0xF4 — npc_rotate_toward_angle
 *
 * Original: FUN_00265cb0
 *
 * Args: expr `target_angle`, expr `rate`.
 *
 * On the first call latches the opcode at +0x1BE and stores
 *   +0x1C4 = rate / fGpffff8d84
 *   +0x1C8 = pack_angle(target_angle / fGpffff8d84)
 *
 * Each call calls FUN_0023a320(npc.facing[+0x5C], target[+0x1C8],
 * rate[+0x1C4]) which returns a per-frame delta (0 when finished).
 * Adds the delta to npc.facing; on 0 returns true and bumps step_counter.
 */

#include <stdbool.h>
#include <stdint.h>

extern short sGpffffbd68;
extern int   iGpffffb0d8;
extern float fGpffff8d84;
extern void  script_eval_expression(void *out);
extern unsigned int angle_pack_fixed(float angle);                       /* FUN_00216690 */
extern float        angle_step_toward(unsigned int cur, unsigned int target,
                                      float rate);                       /* FUN_0023a320 */

bool op_0xF4_npc_rotate_toward_angle(void) {
    short opcode = sGpffffbd68;
    int   target_angle, rate;
    script_eval_expression(&target_angle);
    script_eval_expression(&rate);

    if (*(short *)(intptr_t)(iGpffffb0d8 + 0x1BE) != opcode) {
        *(short *)(intptr_t)(iGpffffb0d8 + 0x1BE) = opcode;
        *(float *)(intptr_t)(iGpffffb0d8 + 0x1C4) = (float)rate / fGpffff8d84;
        *(unsigned int *)(intptr_t)(iGpffffb0d8 + 0x1C8) =
            angle_pack_fixed((float)target_angle / fGpffff8d84);
    }
    float delta = angle_step_toward(
        *(unsigned int *)(intptr_t)(iGpffffb0d8 + 0x5C),
        *(unsigned int *)(intptr_t)(iGpffffb0d8 + 0x1C8),
        *(float        *)(intptr_t)(iGpffffb0d8 + 0x1C4));

    if (delta == 0.0f) {
        ((char *)(intptr_t)iGpffffb0d8)[0x1BC]++;
        *(short *)(intptr_t)(iGpffffb0d8 + 0x1BE) = 0;
        return true;
    }
    *(float *)(intptr_t)(iGpffffb0d8 + 0x5C) += delta;
    return false;
}
