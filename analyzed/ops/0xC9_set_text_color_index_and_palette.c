/*
 * Opcode 0xC9 — set_text_color_index_and_palette
 *
 * Original: FUN_00264470
 *
 * Reads seven expressions:
 *   color_index, c0, c1, c2, c3, c4, ignored
 * Writes DAT_00355661 = color_index (low byte) and copies the five colour
 * shorts c0..c4 into the contiguous DAT_00343878 palette table.
 */

extern unsigned char DAT_00355661;
extern unsigned short DAT_00343878[];
extern void           script_eval_expression(void *out);

int op_0xC9_set_text_color_index_and_palette(void) {
    unsigned char  color_index[16];
    unsigned short colors[8]; /* read as 4-byte slots, 1st short used */
    unsigned char  unused[16];
    script_eval_expression(color_index);
    script_eval_expression(&colors[0]);
    script_eval_expression(&colors[2]);
    script_eval_expression(&colors[4]);
    script_eval_expression(&colors[6]);
    script_eval_expression(unused);
    DAT_00355661 = color_index[0];
    for (int i = 0; i < 5; i++) {
        DAT_00343878[i] = colors[i * 2];
    }
    return 0;
}
