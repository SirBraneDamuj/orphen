/*
 * Opcode 0xD7 — set_palette_slot_byte
 *
 * Original: FUN_00264d90
 *
 * Args: expr `slot` (>7 → diag 0x34d3a8), expr `value`. Writes one byte
 * into the 8-entry palette/parameter table at &gp0xffffb750[slot].
 */

extern unsigned char gp0xffffb750[];
extern void          script_eval_expression(void *out);
extern void          script_diagnostic(unsigned int);

int op_0xD7_set_palette_slot_byte(void) {
    int           slot;
    unsigned char value;
    script_eval_expression(&slot);
    script_eval_expression(&value);
    if (slot > 7) script_diagnostic(0x34d3a8);
    gp0xffffb750[slot] = value;
    return 0;
}
