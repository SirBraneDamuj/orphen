/*
 * Opcode 0xC4 — set_light_intensity
 *
 * Original: FUN_00264218
 *
 * Args: expr `slot` (>0xF → diag 0x34d310), expr `intensity`.
 * Writes light_table[slot].intensity = intensity / DAT_00352cc4.
 */

extern float        DAT_00343898[];
extern float        DAT_00352cc4;
extern void script_eval_expression(void *out);
extern void script_diagnostic(unsigned int);

int op_0xC4_set_light_intensity(void) {
    unsigned int slot;
    int          intensity;
    script_eval_expression(&slot);
    script_eval_expression(&intensity);
    if (slot > 0xF) script_diagnostic(0x34d310);
    DAT_00343898[slot * 5] = (float)intensity / DAT_00352cc4;
    return 0;
}
