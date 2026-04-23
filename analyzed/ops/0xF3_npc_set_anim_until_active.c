/*
 * Opcode 0xF3 — npc_set_anim_until_active
 *
 * Original: FUN_00265c30
 *
 * Args: expr `anim_slot`. On the first call against this npc, latches
 * the opcode at +0x1BE and overwrites npc.anim_slot (+0xA0) with
 * `anim_slot` (when non-negative and different). Returns true once the
 * npc's "active" flag bit (low bit at +0x6) is set; on completion bumps
 * step_counter (+0x1BC) and clears +0x1BE.
 */

#include <stdbool.h>
#include <stdint.h>

extern short sGpffffbd68;
extern int   iGpffffb0d8;
extern void  script_eval_expression(void *out);

bool op_0xF3_npc_set_anim_until_active(void) {
    short opcode = sGpffffbd68;
    int   anim_slot[4];
    script_eval_expression(anim_slot);

    if (*(short *)(intptr_t)(iGpffffb0d8 + 0x1BE) != opcode) {
        *(short *)(intptr_t)(iGpffffb0d8 + 0x1BE) = opcode;
        if ((long)*(short *)(intptr_t)(iGpffffb0d8 + 0xA0) != (long)anim_slot[0]
            && anim_slot[0] >= 0) {
            *(short *)(intptr_t)(iGpffffb0d8 + 0xA0) = (short)anim_slot[0];
        }
    }
    bool active = (*(unsigned short *)(intptr_t)(iGpffffb0d8 + 6) & 1) != 0;
    if (active) {
        ((char *)(intptr_t)iGpffffb0d8)[0x1BC]++;
        *(short *)(intptr_t)(iGpffffb0d8 + 0x1BE) = 0;
    }
    return active;
}
