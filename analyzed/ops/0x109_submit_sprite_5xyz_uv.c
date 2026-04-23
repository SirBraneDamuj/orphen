/*
 * Opcode 0x109 — submit_sprite_5xyz_uv
 *
 * Original: FUN_002625b8
 *
 * Reads 4 raw exprs (color/uv pair packed first), then 2 coordinates,
 * then an entity index. Selects an entity from the 256-entry pool
 * (negative or >=0x100 → NULL). Forwards to FUN_0021bd30 with three
 * coords scaled by DAT_00352c6c.
 */

#include <stdint.h>

extern unsigned short *DAT_00355044;             /* selected_entity */
extern unsigned short  DAT_0058beb0[];           /* entity_pool */
extern float           DAT_00352c6c;
extern void            script_eval_expression(void *out);                /* FUN_0025c258 */
extern void            FUN_0021bd30(float x, float y, float z,
                                    unsigned int p0, unsigned int p1,
                                    void *entity);

unsigned int op_0x109_submit_sprite_5xyz_uv(void) {
    unsigned short *entity = DAT_00355044;
    unsigned int  args[6];
    int           pool_index;
    script_eval_expression(&args[0]);
    script_eval_expression(&args[1]);
    script_eval_expression(&args[2]);
    script_eval_expression(&args[3]);
    int z;
    script_eval_expression(&z);
    script_eval_expression(&pool_index);
    if (pool_index < 0)        entity = (unsigned short *)0;
    else if (pool_index < 256) entity = &DAT_0058beb0[pool_index * 0xEC / 2];
    FUN_0021bd30((float)(int)args[1] / DAT_00352c6c,
                 (float)(int)args[2] / DAT_00352c6c,
                 (float)z          / DAT_00352c6c,
                 args[0], args[3], entity);
    return 0;
}
