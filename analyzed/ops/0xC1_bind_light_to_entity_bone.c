/*
 * Opcode 0xC1 — bind_light_to_entity_bone
 *
 * Original: FUN_00263fe8
 *
 * Args (8 expressions): selector, x, y, z, r, g, b, intensity.
 *
 * Selects the entity. If its "owned light slot" byte at +0x195 is unset
 * (sign bit), allocates a point light (FUN_00266050) and stashes the
 * 0..2 slot index back at +0x195 (slots >2 → diag 0x34d2d0).
 *
 * Reads the bone position via entity_get_bone_world (FUN_0020dc88) into
 * the corresponding light_table[slot] xyz fields, then fills in the
 * RGB bytes and intensity (scaled by fGpffff8d50). Returns the slot.
 */

#include <stdint.h>

extern int   iGpffffb0d4;
extern float fGpffff8d50;
extern unsigned char DAT_00343894[], DAT_00343895[], DAT_00343896[];
extern float         DAT_00343888[];   /* light xyz base, stride 5 floats */
extern float         DAT_00343898[];   /* light intensity, stride 5 floats */
extern void script_eval_expression(void *out);
extern void script_select_entity(int i, int prev);
extern void script_diagnostic(unsigned int);
extern long light_alloc_point(void);
extern void entity_get_bone_world(int entity, long bone, void *xyz, void *out_xyz);

unsigned char op_0xC1_bind_light_to_entity_bone(void) {
    int prev_entity = iGpffffb0d4;
    int selector, x, y, z;
    unsigned char r, g, b;
    int           intensity;
    script_eval_expression(&selector);
    script_eval_expression(&x);
    script_eval_expression(&y);
    script_eval_expression(&z);
    script_eval_expression(&r);
    script_eval_expression(&g);
    script_eval_expression(&b);
    script_eval_expression(&intensity);
    script_select_entity(selector, prev_entity);

    if ((signed char)*(unsigned char *)(intptr_t)(iGpffffb0d4 + 0x195) < 0) {
        long slot = light_alloc_point();
        if (slot < 3) *(unsigned char *)(intptr_t)(iGpffffb0d4 + 0x195) = (unsigned char)slot;
        else          script_diagnostic(0x34d2d0);
    }

    int           slot     = (int)*(signed char *)(intptr_t)(iGpffffb0d4 + 0x195);
    int           byte_off = slot * 0x14;
    float         pos[3]   = {
        (float)x / fGpffff8d50,
        (float)y / fGpffff8d50,
        (float)z / fGpffff8d50,
    };
    entity_get_bone_world(iGpffffb0d4, -1, pos, &DAT_00343888[slot * 5]);
    DAT_00343894[byte_off] = r;
    DAT_00343896[byte_off] = b;
    DAT_00343895[byte_off] = g;
    DAT_00343898[slot * 5] = (float)intensity / fGpffff8d50;
    return *(unsigned char *)(intptr_t)(iGpffffb0d4 + 0x195);
}
