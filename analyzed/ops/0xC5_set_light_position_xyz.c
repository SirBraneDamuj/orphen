/*
 * Opcode 0xC5 — set_light_position_xyz
 *
 * Original: FUN_00264298
 *
 * Args: expr `slot` (>0xF → diag 0x34d330), expr x, y, z. Writes the
 * three floats (each scaled by 1/DAT_00352cc8) into light_table[slot]
 * xyz.
 */

extern float DAT_00343888[], DAT_0034388c[], DAT_00343890[];
extern float DAT_00352cc8;
extern void  script_eval_expression(void *out);
extern void  script_diagnostic(unsigned int);

int op_0xC5_set_light_position_xyz(void) {
    unsigned int slot;
    int          x, y, z;
    script_eval_expression(&slot);
    script_eval_expression(&x);
    script_eval_expression(&y);
    script_eval_expression(&z);
    if (slot > 0xF) script_diagnostic(0x34d330);
    DAT_00343888[slot * 5] = (float)x / DAT_00352cc8;
    DAT_0034388c[slot * 5] = (float)y / DAT_00352cc8;
    DAT_00343890[slot * 5] = (float)z / DAT_00352cc8;
    return 0;
}
