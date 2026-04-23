/*
 * Opcode 0xB4 — set_entity_anim_channel
 *
 * Original: FUN_00263a58
 *
 * Args: expr `selector`, expr `anim_slot`, inline byte `channel`,
 *       expr `value`.
 *
 * Like 0xB2 but writes a single float at snapshot[channel] (an 8-float
 * array starting at the snapshot base) scaled by fGpffff8d3c.
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
