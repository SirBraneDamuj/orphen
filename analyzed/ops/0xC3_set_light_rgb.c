/*
 * Opcode 0xC3 — set_light_rgb
 *
 * Original: FUN_00264190
 *
 * Args: expr `slot` (>0xF → diag 0x34d2f0), expr r, g, b. Writes the RGB
 * bytes into the light slot.
 */

extern unsigned char DAT_00343894[], DAT_00343895[], DAT_00343896[];
extern void script_eval_expression(void *out);
extern void script_diagnostic(unsigned int);

int op_0xC3_set_light_rgb(void) {
    unsigned int  slot;
    unsigned char r, g, b;
    script_eval_expression(&slot);
    script_eval_expression(&r);
    script_eval_expression(&g);
    script_eval_expression(&b);
    if (slot > 0xF) script_diagnostic(0x34d2f0);
    int byte_off = (int)slot * 0x14;
    DAT_00343894[byte_off] = r;
    DAT_00343895[byte_off] = g;
    DAT_00343896[byte_off] = b;
    return 0;
}
