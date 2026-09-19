/*
 * Opcode 0xE5 - load_backdrop_model
 *
 * Original: FUN_002651a0
 *
 * CORRECTED 2026-09-19. This was filed as "emit_audio_pair_with_inline_byte"
 * and has nothing to do with audio. It loads a **PSB4 backdrop model** into one
 * of four background slots.
 *
 * Operands, in stream order:
 *   - two inline bytes, little-endian, sign-extended to 16 bits: the resource
 *     id of a PSB4 record in the scene bundle's category 2 (the same category
 *     the PSM2 map lives in). The old note said one byte; 0x002651B4..C8 reads
 *     two and ors them, `lbu`/`lbu`/`sll 8`/`or`, then `sll 16 / sra 16`.
 *   - one expression: the slot, 0..3.
 *
 * Forwards to FUN_0022cd88(id, slot), which is FUN_00223268(2, id, arena)
 * followed by FUN_0022ce60(arena, slot) -- the PSB4 parse, which checks the
 * 'PSB4' magic (0x34425350) and fills descriptor `slot` of the four-entry
 * table at DAT_00345a18 (stride 0x24).
 *
 * A scene does not have to use this opcode to get a backdrop: FUN_0022a418:134
 * calls FUN_0022cde8(sceneDescriptor, 0), which reads the **halfword at scene
 * descriptor +8** and installs that id in slot 0. s01_e013 gets its fog
 * cylinder (id 0x009E) that way and never issues 0xE5.
 *
 * See analyzed/ops/0xE6_set_backdrop_slot_shade.c for the other half, and
 * FUN_0020c290 / FUN_0020c2f0 for the draw.
 */

extern unsigned char *DAT_00355cd0;  /* script byte cursor */
extern void script_eval_expression(void *out);
extern void FUN_0022cd88(short resourceId, int slot);

int op_0xE5_load_backdrop_model(void) {
    short resourceId = (short)(DAT_00355cd0[0] | (DAT_00355cd0[1] << 8));
    DAT_00355cd0 += 2;

    int slot[4];
    script_eval_expression(slot);

    FUN_0022cd88(resourceId, slot[0]);
    return 0;
}
