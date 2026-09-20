/*
 * Opcode 0x10E - open_plume_emitter
 *
 * Original: FUN_00262a98
 *
 * Opens one emitter in the plume pool -- the tenth particle system, 100
 * emitters of 0x3C shedding into 1000 puffs of 0x24. See
 * analyzed/plume_particle_pool.c for the pool itself. In s01_e013 this is the
 * fire and the smoke on the wrecked ship.
 *
 * Ten expressions, and **read order is not call order**: the burst count is
 * read first and passed sixth, and the life is read fourth and passed seventh.
 * The five divided by fGpffff8d10 (100000.0) are the rise, the size and the
 * three coordinates; the rest go through raw -- the count and the life as
 * halfwords, the cycle count and the mode as bytes, the colour whole.
 *
 * The earlier name here, "submit_five_coords_5params", described the shape and
 * not the job: three of the "five coords" are a position, and the two
 * "params" that are not are a rise rate and a sprite size.
 *
 *   cycles  99 is immortal -- FUN_0021FB68 only decrements +0x38 below 0x63
 *   mode    0 plain, 1 adds the flame at the base, 2 is a one-shot burst
 *   colour  zero takes the sheet's own palette and the record's CLUT bank
 */

extern float fGpffff8d10;      /* 100000.0 */
extern void  script_eval_expression(void *out);
extern void  FUN_0021f6e8(float riseSpeed, float size,
                          float x, float y, float z,
                          unsigned short burstCount, unsigned short lifeUnits,
                          unsigned char cycles, unsigned char mode,
                          unsigned int colour);

unsigned int op_0x10E_open_plume_emitter(void) {
    unsigned short burstCount;
    int            riseSpeed, size;
    unsigned short lifeUnits;
    int            x, y, z;
    unsigned char  cycles, mode;
    unsigned int   colour;

    script_eval_expression(&burstCount);
    script_eval_expression(&riseSpeed);
    script_eval_expression(&size);
    script_eval_expression(&lifeUnits);
    script_eval_expression(&x);
    script_eval_expression(&y);
    script_eval_expression(&z);
    script_eval_expression(&cycles);
    script_eval_expression(&mode);
    script_eval_expression(&colour);

    FUN_0021f6e8((float)riseSpeed / fGpffff8d10, (float)size / fGpffff8d10,
                 (float)x / fGpffff8d10, (float)y / fGpffff8d10,
                 (float)z / fGpffff8d10,
                 burstCount, lifeUnits, cycles, mode, colour);
    return 0;
}
