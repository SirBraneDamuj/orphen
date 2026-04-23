/*
 * Opcodes 0xEE / 0xEF / 0xF0 / 0xF1 — npc_step_toward_xy (shared handler)
 *
 * Original: FUN_002658c0  (all four opcodes dispatch here)
 *
 * Drives focus_npc (psGpffffb0d8) toward a target XY at a per-opcode
 * speed. Bytecode args read in this order:
 *   0xEE: expr anim_slot, expr speed_param, expr target_x, expr target_y
 *   0xEF: (no anim_slot)  expr speed_param, expr target_x, expr target_y
 *   0xF0: (no anim_slot, no speed param) expr target_x, expr target_y
 *         Forces anim_slot = 4, step = frame_ticks * 48 * fGpffff8d78.
 *   0xF1: as 0xF0 but step = frame_ticks * 128 * fGpffff8d7c, and
 *         anim_slot = (kind > 6) ? 8 : 14, where `kind` comes from
 *         entity_lookup_by_type(entity.type) (handles type-0x38 alias).
 *
 * For 0xEE/0xEF the per-frame step is
 *   step = (speed_param/fGpffff8d74) * frame_ticks * (1/32)
 *
 * Common phase, after step is computed:
 *   - If npc.last_op (+0x1BE) != current_opcode, latch current_opcode
 *     and (when not 0xEF) overwrite npc.anim_slot (+0xA0) with anim_slot
 *     iff the new value differs and is non-negative.
 *   - delta = (target / fGpffff8d80) - npc.position_xy (+0x20/+0x24)
 *   - distance = magnitude(delta); facing = atan2(dy, dx)
 *   - If distance < step: snap by incrementing npc.step_counter (+0x1BC),
 *     clear npc.last_op, return 1 (arrived).
 *   - Else: clamp distance to step. For non-0xEF set npc.facing (+0x5C)
 *     to the computed angle. Translate npc XY by step * (cos, sin).
 *   - When npc.flags2 & 0x1000 and npc.something (+0x68) == 0 and
 *     npc.flags3 & 0x100 and npc.flags4 & 8, also call
 *     npc_sync_follow + audio_play_for_entity (run/walk variant chosen by
 *     `current_opcode == 0xF1`).
 *   - Returns 0 while still moving, 1 on arrival.
 */

#include <stdint.h>

extern short          sGpffffbd68;          /* current opcode */
extern short         *psGpffffb0d8;         /* focus npc entity */
extern int            iGpffffb64c;          /* frame ticks */
extern float          fGpffff8d74, fGpffff8d78, fGpffff8d7c, fGpffff8d80;
extern void           script_eval_expression(void *out);
extern long           entity_lookup_by_type(short type);                /* FUN_002298d0 */
extern int            angle_from_vector(float dy, float dx);            /* FUN_00305408 */
extern float          vector_magnitude_xy(float x, float y);            /* FUN_00216608 */
extern float          cos_fixed(int angle);                             /* FUN_00305130 */
extern float          sin_fixed(int angle);                             /* FUN_00305218 */
extern unsigned long  npc_sync_follow(void *entity, int run_mode);      /* FUN_00255d88 */
extern void           audio_play_for_entity(unsigned long tag, void *entity); /* FUN_00267d38 */

unsigned int op_0xEE_0xEF_0xF0_0xF1_npc_step_toward_xy(void) {
    short opcode = sGpffffbd68;
    int   anim_slot = 0;
    float step      = 0.0f;
    int   target_x, target_y;

    if (opcode == 0xF0 || opcode == 0xF1) {
        if (opcode == 0xF0) {
            anim_slot = 4;
            step = (float)iGpffffb64c * 48.0f * fGpffff8d78;
        } else {
            short type = *psGpffffb0d8;
            if (type == 0x38) type = psGpffffb0d8[0xE7];
            long kind = entity_lookup_by_type(type);
            anim_slot = (kind > 6) ? 8 : 14;
            step = (float)iGpffffb64c * 128.0f * fGpffff8d7c;
        }
    } else {
        if (opcode == 0xEE) script_eval_expression(&anim_slot);
        int speed_param;
        script_eval_expression(&speed_param);
        step = ((float)speed_param / fGpffff8d74) * (float)iGpffffb64c * 0.03125f;
    }
    script_eval_expression(&target_x);
    script_eval_expression(&target_y);

    if (psGpffffb0d8[0xDF] != opcode) {
        psGpffffb0d8[0xDF] = opcode;
        if (opcode != 0xEF) {
            if ((long)psGpffffb0d8[0x50] != (long)anim_slot && anim_slot >= 0) {
                psGpffffb0d8[0x50] = (short)anim_slot;
            }
        }
    }

    float dx = (float)target_x / fGpffff8d80 - *(float *)(psGpffffb0d8 + 0x10);
    float dy = (float)target_y / fGpffff8d80 - *(float *)(psGpffffb0d8 + 0x12);
    int   facing = angle_from_vector(dy, dx);
    float dist   = vector_magnitude_xy(dx, dy);

    unsigned int arrived = 0;
    float        move    = step;
    if (dist < step) {
        arrived = 1;
        ((char *)psGpffffb0d8)[0x1BC]++;
        psGpffffb0d8[0xDF] = 0;
        move = dist;
    } else if (opcode != 0xEF) {
        *(int *)(psGpffffb0d8 + 0x2E) = facing;
    }

    *(float *)(psGpffffb0d8 + 0x18) += move * cos_fixed(facing);
    unsigned short flags2 = psGpffffb0d8[2];
    *(float *)(psGpffffb0d8 + 0x1A) += move * sin_fixed(facing);

    if ((flags2 & 0x1000) && *(int *)(psGpffffb0d8 + 0x34) == 0
        && (psGpffffb0d8[0x55] & 0x100) && (psGpffffb0d8[3] & 8)) {
        unsigned long tag = npc_sync_follow(psGpffffb0d8, opcode == 0xF1);
        audio_play_for_entity(tag, psGpffffb0d8);
    }
    return arrived;
}
