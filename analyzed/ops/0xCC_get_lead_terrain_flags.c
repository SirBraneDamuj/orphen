/*
 * Opcode 0xCC — get_lead_terrain_flags
 *
 * Original: FUN_002645b8
 *
 * Reads one expression `selector` (then immediately re-selects the lead
 * at 0x58beb0 — the original code uses the lead, not the selector, so
 * this opcode just queries the current lead's terrain). Calls
 * terrain_query_xyz (FUN_00227798) at the lead position; returns 0 if
 * DAT_00354d4e (terrain index) is < 0, otherwise returns the terrain
 * record's flags word at &DAT_003556b0[index*0x78 + 4].
 */

#include <stdint.h>

extern int          DAT_003556b0;
extern short        DAT_00354d4e;
extern float        DAT_0058bed0, DAT_0058bed4, DAT_0058bed8;
extern void script_eval_expression(void *out);
extern void script_select_entity(int i, void *prev);
extern void terrain_query_xyz(float x, float y, float z);

unsigned int op_0xCC_get_lead_terrain_flags(void) {
    int selector[4];
    script_eval_expression(selector);
    script_select_entity(selector[0], (void *)0x58beb0);
    terrain_query_xyz(DAT_0058bed0, DAT_0058bed4, DAT_0058bed8);
    if (DAT_00354d4e < 0) return 0;
    return *(unsigned int *)(DAT_003556b0 +
                             ((int)DAT_00354d4e & 0x3FFF) * 0x78 + 4);
}
