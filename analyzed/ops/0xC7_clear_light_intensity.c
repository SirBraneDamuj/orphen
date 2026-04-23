/*
 * Opcode 0xC7 — clear_light_intensity
 *
 * Original: FUN_002643f0
 *
 * Args: expr `slot` (>0xF → diag 0x34d330). Sets light_table[slot]
 * intensity to 0 (effectively disabling the light without releasing the
 * slot).
 */

extern float DAT_00343898[];
extern void  script_eval_expression(void *out);
extern void  script_diagnostic(unsigned int);

int op_0xC7_clear_light_intensity(void) {
    unsigned int slot[4];
    script_eval_expression(slot);
    if (slot[0] > 0xF) script_diagnostic(0x34d330);
    DAT_00343898[slot[0] * 5] = 0.0f;
    return 0;
}
