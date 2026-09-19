/*
 * Opcode 0xE6 - set_backdrop_slot_shade
 *
 * Original: FUN_00265200
 *
 * CORRECTED 2026-09-19. This was filed as "set_minimap_marker_slot"; the
 * four-entry table at DAT_00345a18 is not minimap markers, it is the four
 * **background model slots** FUN_0020c290 draws (see
 * analyzed/ops/0xE5_load_backdrop_model.c).
 *
 * Three expressions, in stream order:
 *   slot   0..3, `> 3` trips the diagnostic at 0x34d3d0
 *   shade  -> descriptor +0x20. FUN_0020c2f0 opens with
 *            `if (*(byte *)(desc + 0x20) == 0) return;`, so **0 turns the
 *            backdrop off** and non-zero turns it on. The value is also handed
 *            to VU1, where FUN_0020c2f0 zeroes it again if it is above 0x7F --
 *            and FUN_0022ce60 seeds slot 0 with exactly 0x80, so the default
 *            install is "on, VU1 parameter 0".
 *   angle  -> descriptor +0x1C, divided by DAT_00352cd4. FUN_0020c2f0 spends it
 *            as FUN_0020bae0(angle, matrix), a rotation about Z: this is how a
 *            scene spins its sky.
 *
 * The other three slots are seeded to 0 by FUN_0022ce60, so a backdrop loaded
 * into slot 1..3 stays invisible until this opcode raises its shade byte.
 */

extern unsigned char DAT_00345a38[];  /* descriptor +0x20, stride 0x24 */
extern float         DAT_00345a34[];  /* descriptor +0x1C, stride 0x24 */
extern float         DAT_00352cd4;
extern void script_eval_expression(void *out);
extern void script_diagnostic(unsigned int);

int op_0xE6_set_backdrop_slot_shade(void) {
    int           slot;
    unsigned char shade;
    int           angle;

    script_eval_expression(&slot);
    script_eval_expression(&shade);
    script_eval_expression(&angle);

    if (slot > 3) script_diagnostic(0x34d3d0);

    int byte_off = slot * 0x24;
    DAT_00345a38[byte_off]                      = shade;
    *(float *)((char *)DAT_00345a34 + byte_off) = (float)angle / DAT_00352cd4;
    return 0;
}
