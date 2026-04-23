/*
 * Opcodes 0xBF / 0xC0 — allocate_light_slot (shared handler)
 *
 * Original: FUN_00263f28
 *
 * Reads four expressions: r, g, b, intensity. Allocates an entry in the
 * 16-slot light table:
 *   0xBF → light_alloc_directional (FUN_00266008)
 *   0xC0 → light_alloc_point       (FUN_00266050)
 *
 * On success (slot >= 0) writes RGB bytes into &DAT_00343894+slot*0x14
 * and the float intensity (`intensity / fGpffff8d4c`) into
 * &DAT_00343898+slot*0x14. Returns the slot index (or -1 on failure).
 */

extern short sGpffffbd68;                       /* current_opcode_id */
extern unsigned char DAT_00343894[], DAT_00343895[], DAT_00343896[];
extern float         DAT_00343898[];            /* stride 0x14 / 5 floats */
extern float         fGpffff8d4c;
extern void script_eval_expression(void *out);
extern long light_alloc_directional(void);
extern long light_alloc_point(void);

long op_0xBF_0xC0_allocate_light_slot(void) {
    short        opcode = sGpffffbd68;
    unsigned char rgb[4];
    int          intensity;
    script_eval_expression(&rgb[0]);
    script_eval_expression(&rgb[1]);
    script_eval_expression(&rgb[2]);
    script_eval_expression(&intensity);

    long slot = (opcode == 0xBF) ? light_alloc_directional()
                                 : light_alloc_point();
    if (slot >= 0) {
        int byte_off = (int)slot * 0x14;
        DAT_00343894[byte_off] = rgb[0];
        DAT_00343895[byte_off] = rgb[1];
        DAT_00343896[byte_off] = rgb[2];
        DAT_00343898[(int)slot * 5] = (float)intensity / fGpffff8d4c;
    }
    return slot;
}
