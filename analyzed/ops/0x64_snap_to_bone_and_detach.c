/*
 * Opcode 0x64 — snap_to_bone_and_detach
 *
 * Original: FUN_0025f700
 *
 * The currently selected entity holds two binding fields:
 *   +0x192 (short): source pool index of an entity to snap to (-1 = none)
 *   +0x194 (s8):    bone/channel index on the source entity (-1 = none)
 *
 * Reads one expression (entity selector) and uses it to set the current
 * entity. If the source pool index is valid (>=0):
 *   - When the bone index is also valid, copy the entity's current world
 *     position from +0x20..+0x2B into a scratch buffer, ask the source
 *     entity for its bone world position (which writes into +0x20..+0x2B),
 *     copy the bone position back into the entity's position fields, then
 *     refresh the terrain reference at +0x4c.
 *   - Always clear the source pool index (+0x192) to 0xFFFF so the binding
 *     fires only once.
 */

#include <stdint.h>

extern short *selected_entity;      /* DAT_00355044 */
extern short  entity_pool[];        /* DAT_0058beb0, stride 0xEC */

extern void   script_eval_expression(void *out); /* FUN_0025c258 */
extern void   script_select_entity(int idx, void *prev); /* FUN_0025d6c0 */
extern void   memcopy_entity(void *dst, void *src, int n); /* FUN_00267da0 */
extern void   entity_get_bone_world(void *src_entity, unsigned char bone,
                                    void *in_pos, void *out_xyz); /* FUN_0020dc88 */
extern unsigned int terrain_ref_from_xyz(float x, float y, float z); /* FUN_00227798 */

int op_0x64_snap_to_bone_and_detach(void) {
    int   selector;
    unsigned char pos_backup[16];
    unsigned char bone_xyz[16];

    script_eval_expression(&selector);
    script_select_entity(selector, selected_entity);

    short src_idx = *(short *)((intptr_t)selected_entity + 0x192);
    if (src_idx < 0) return 0;

    signed char bone = *(signed char *)((intptr_t)selected_entity + 0x194);
    if (bone >= 0) {
        void *pos_ptr = (void *)((intptr_t)selected_entity + 0x20);
        memcopy_entity(pos_backup, pos_ptr, 0xC);
        entity_get_bone_world(&entity_pool[src_idx * 0xEC / 2],
                              (unsigned char)bone, pos_ptr, bone_xyz);
        memcopy_entity(pos_ptr, bone_xyz, 0xC);

        float fx = *(float *)((intptr_t)selected_entity + 0x20);
        float fy = *(float *)((intptr_t)selected_entity + 0x24);
        float fz = *(float *)((intptr_t)selected_entity + 0x28);
        *(unsigned int *)((intptr_t)selected_entity + 0x4C) =
            terrain_ref_from_xyz(fx, fy, fz);
    }
    *(unsigned short *)((intptr_t)selected_entity + 0x192) = 0xFFFF;
    return 0;
}
