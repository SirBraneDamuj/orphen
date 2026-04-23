/*
 * Opcode 0xF2 — npc_anim_slot_for_duration
 *
 * Original: FUN_00265b90
 *
 * Args: expr `anim_slot`, expr `duration_ticks`. Tracks itself with the
 * focus npc's (+0x1BE) "last opcode" latch and (+0x1C0) countdown:
 *   - First time this opcode runs against a given npc: latch the opcode,
 *     write `duration_ticks` into +0x1C0 and (if positive and different)
 *     overwrite the npc.anim_slot (+0xA0) with `anim_slot`.
 *   - On every call: decrement +0x1C0. When it reaches 0 increment the
 *     npc.step_counter (+0x1BC), clear +0x1BE and return true (done).
 */

#include <stdbool.h>
#include <stdint.h>

extern short  sGpffffbd68;
extern int    iGpffffb0d8;
extern void   script_eval_expression(void *out);

bool op_0xF2_npc_anim_slot_for_duration(void) {
    short  opcode      = sGpffffbd68;
    int    anim_slot;
    short  duration;
    script_eval_expression(&anim_slot);
    script_eval_expression(&duration);

    short remaining;
    if (*(short *)(intptr_t)(iGpffffb0d8 + 0x1BE) == opcode) {
        remaining = *(short *)(intptr_t)(iGpffffb0d8 + 0x1C0);
    } else {
        *(short *)(intptr_t)(iGpffffb0d8 + 0x1BE) = opcode;
        *(short *)(intptr_t)(iGpffffb0d8 + 0x1C0) = duration;
        if ((long)*(short *)(intptr_t)(iGpffffb0d8 + 0xA0) != (long)anim_slot
            && anim_slot >= 0) {
            *(short *)(intptr_t)(iGpffffb0d8 + 0xA0) = (short)anim_slot;
        }
        remaining = duration;
    }

    bool done = remaining <= 1;
    *(unsigned short *)(intptr_t)(iGpffffb0d8 + 0x1C0) = (unsigned short)(remaining - 1);
    if (done) {
        ((char *)(intptr_t)iGpffffb0d8)[0x1BC]++;
        *(short *)(intptr_t)(iGpffffb0d8 + 0x1BE) = 0;
    }
    return done;
}
