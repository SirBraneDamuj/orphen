/*
 * Opcode 0xB4 — set_entity_anim_channel
 *
 * Original: FUN_00263a58
 *
 * Args: expr `selector`, expr `anim_slot`, inline byte `channel`,
 *       expr `value`.
 *
 * CORRECTED (checked against FUN_0020da68 / FUN_0020d8c0): `anim_slot` is a
 * BONE index and the "snapshot" is that bone's pose, sampled at frame 0 of
 * entity +0xA0 in FUN_0020d8c0's order -- rotation xyz (0..2), translation
 * xyz (3..5), scale (6). The patched pose is installed as a scripted bone
 * override (DAT_004a7e00) with duration 0, held until 0xB1 clears it.
 * fGpffff8d3c (0x00352CAC) is 100000.0, as are 0xB2's and 0xB3's scales.
 */

#include <stdint.h>

extern int  selected_entity_as_int;
extern unsigned char *script_byte_cursor;
extern void script_eval_expression(void *out);
extern void script_select_entity(int i, void *prev);
extern void mem_zero(void *ptr, int n);
extern void anim_snapshot_read(void *entity, int anim_slot,
                               short channel, int zero, void *buf);
extern void anim_snapshot_apply(void *entity, int anim_slot,
                                void *buf, int zero);
extern float fGpffff8d3c;

int op_0xB4_set_entity_anim_channel(void) {
    int   selector, anim_slot, value;
    int   prev_entity = selected_entity_as_int;
    float snapshot[8];
    script_eval_expression(&selector);
    script_eval_expression(&anim_slot);
    unsigned char channel = *script_byte_cursor++;
    script_eval_expression(&value);
    script_select_entity(selector, (void *)(intptr_t)prev_entity);
    mem_zero(snapshot, 0x1C);
    void *entity = (void *)(intptr_t)selected_entity_as_int;
    anim_snapshot_read(entity, anim_slot,
                       *(short *)((intptr_t)entity + 0xA0), 0, snapshot);
    snapshot[channel] = (float)value / fGpffff8d3c;
    anim_snapshot_apply(entity, anim_slot, snapshot, 0);
    return 0;
}
