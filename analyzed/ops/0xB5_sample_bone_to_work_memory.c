/*
 * Opcode 0xB5 — sample_bone_to_work_memory
 *
 * Original: FUN_00263b18
 *
 * Args: expr `selector`, expr `bone`, expr `dest_slot`. Bounds check:
 * (dest_slot - 3) > 0x7F → diag 0x34d290.
 *
 * Selects the entity, queries entity_get_bone_world for the bone's world
 * position into a local 3-float buffer, then packs each component (x,y,z)
 * by `pack_float_to_fixed(value * fGpffff8d40)` and writes the three
 * fixed-point words into work memory at iGpffffb0f0[dest_slot*4 .. +8].
 */

#include <stdint.h>

extern unsigned int uGpffffb0d4;
extern int          iGpffffb0f0;
extern float        fGpffff8d40;
extern void script_eval_expression(void *out);
extern void script_select_entity(int i, void *prev);
extern void script_diagnostic(unsigned int);
extern void scratch_zero_32(void *dst, int val);  /* FUN_00267e78 */
extern void entity_get_bone_world(unsigned int ent_u, int bone,
                                  void *scratch, float *outXYZ);
extern unsigned int pack_float_to_fixed(float v);

int op_0xB5_sample_bone_to_work_memory(void) {
    int selector, bone, dest_slot;
    unsigned char scratch[0x10];
    float xyz[3];
    script_eval_expression(&selector);
    script_eval_expression(&bone);
    script_eval_expression(&dest_slot);
    if (dest_slot - 3 > 0x7F) script_diagnostic(0x34d290);

    script_select_entity(selector, (void *)(intptr_t)uGpffffb0d4);
    scratch_zero_32(scratch, 0x20);
    entity_get_bone_world(uGpffffb0d4, bone, scratch, xyz);

    unsigned int *dst = (unsigned int *)(dest_slot * 4 + iGpffffb0f0);
    dst[0] = pack_float_to_fixed(xyz[0] * fGpffff8d40);
    dst[1] = pack_float_to_fixed(xyz[1] * fGpffff8d40);
    dst[2] = pack_float_to_fixed(xyz[2] * fGpffff8d40);
    return 0;
}
