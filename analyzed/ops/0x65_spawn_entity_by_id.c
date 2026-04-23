/*
 * Opcode 0x65 — spawn_entity_by_id
 *
 * Original: FUN_0025f7d8
 *
 * Bytecode layout (after dispatch):
 *   u16 little-endian id  (inline at script IP, 2 bytes)
 *   expr counter          (read by script_read_ip_offset; > 0xFFFF → diag 0x34ced8)
 *   expr anim             (short)
 *   expr x, expr y, expr z (int, divided by spawn coord scale 0x352bdc → world units)
 *   expr angle            (int, divided by same scale)
 *
 * Allocates a new pooled entity by id (FUN_00265e28). On success:
 *   type word     = 0x38            (spawned/instance)
 *   flags         |= 0x4000
 *   position      = (fx, fy, fz)
 *   anim slot     = anim
 *   terrain ref   = terrain_ref_for_xy(fx, fy, entity)   at +0x4c
 *   counter       (low 16 bits) at +0x130
 *   id high byte  at +0x1ce
 *   facing angle  packed by FUN_00216690 at +0x5c
 *
 * Returns true if an entity was allocated.
 */

#include <stdbool.h>
#include <stdint.h>

extern short *selected_entity;             /* DAT_00355044 */
extern unsigned char *script_byte_cursor;  /* DAT_00355cd0 */

extern void  script_eval_expression(void *out);             /* FUN_0025c258 */
extern int   script_read_ip_offset(void);                   /* FUN_0025c1d0 */
extern void  script_diagnostic(unsigned int msg);           /* FUN_0026bfc0 */
extern void *entity_alloc_by_id(int id);                    /* FUN_00265e28 */
extern unsigned int terrain_ref_for_xy(float x, float y, void *entity); /* FUN_00227070 */
extern unsigned int angle_pack_fixed(float angle);          /* FUN_00216690 */

extern float DAT_00352bdc;   /* spawn coord scale (4096.0) */

bool op_0x65_spawn_entity_by_id(void) {
    /* Inline u16 id (little-endian). */
    unsigned int id = (unsigned int)script_byte_cursor[0] |
                      ((unsigned int)script_byte_cursor[1] << 8);
    script_byte_cursor += 2;

    unsigned long counter = (unsigned long)script_read_ip_offset();
    if (counter > 0xFFFF) script_diagnostic(0x34ced8);

    short anim;
    int   x, y, z, angle;
    script_eval_expression(&anim);
    script_eval_expression(&x);
    script_eval_expression(&y);
    script_eval_expression(&z);
    script_eval_expression(&angle);

    void *entity = entity_alloc_by_id((int)id);
    selected_entity = (short *)entity;
    if (!entity) return false;

    float scale = DAT_00352bdc;
    float fx = (float)x / scale;
    float fy = (float)y / scale;
    float fz = (float)z / scale;
    float fa = (float)angle / scale;

    short *e = (short *)entity;
    e[0]    = 0x38;
    e[1]   |= 0x4000;
    *(float *)(e + 0x10) = fx;     /* +0x20 */
    *(float *)(e + 0x12) = fy;     /* +0x24 */
    *(float *)(e + 0x14) = fz;     /* +0x28 */
    e[0x50] = anim;                /* +0xA0 */
    *(unsigned int *)(e + 0x26) = terrain_ref_for_xy(fx, fy, entity); /* +0x4c */
    e[0x98]  = (short)counter;     /* +0x130 */
    e[0xE7]  = (short)id;          /* +0x1ce — high id byte */
    *(unsigned int *)(e + 0x2E) = angle_pack_fixed(fa);               /* +0x5c */
    return true;
}
