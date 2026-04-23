/*
 * Opcode 0xB3 — set_entity_anim_xyz_rate
 *
 * Original: FUN_00263978
 *
 * Args: expr `selector`, expr `anim_slot`, expr `rx`, `ry`, `rz`.
 *
 * Like 0xB2 but writes three floats (x/y/z rate) at snapshot offsets
 * +0x0C/0x10/0x14, scaled by fGpffff8d38.
 */

#include <stdint.h>

extern int  selected_entity_as_int;
extern void script_eval_expression(void *out);
extern void script_select_entity(int i, void *prev);
extern void mem_zero(void *ptr, int n);
extern void anim_snapshot_read(void *entity, int anim_slot,
                               short channel, int zero, void *buf);
extern void anim_snapshot_apply(void *entity, int anim_slot,
                                void *buf, int zero);
extern float fGpffff8d38;

int op_0xB3_set_entity_anim_xyz_rate(void) {
    int selector, anim_slot, rx, ry, rz;
    int prev_entity = selected_entity_as_int;
    unsigned char snapshot[0x1C];
    script_eval_expression(&selector);
    script_eval_expression(&anim_slot);
    script_eval_expression(&rx);
    script_eval_expression(&ry);
    script_eval_expression(&rz);
    script_select_entity(selector, (void *)(intptr_t)prev_entity);
    mem_zero(snapshot, 0x1C);
    void *entity = (void *)(intptr_t)selected_entity_as_int;
    anim_snapshot_read(entity, anim_slot,
                       *(short *)((intptr_t)entity + 0xA0), 0, snapshot);
    *(float *)(snapshot + 0x0C) = (float)rx / fGpffff8d38;
    *(float *)(snapshot + 0x10) = (float)ry / fGpffff8d38;
    *(float *)(snapshot + 0x14) = (float)rz / fGpffff8d38;
    anim_snapshot_apply(entity, anim_slot, snapshot, 0);
    return 0;
}
