/*
 * Opcode 0xE6 — set_minimap_marker_slot
 *
 * Original: FUN_00265200
 *
 * Args: expr `slot` (>3 → diag 0x34d3d0), inline byte `flag`, expr
 * `value`. Writes:
 *   minimap_table[slot].flag  (&DAT_00345a38 + slot*0x24) = flag
 *   minimap_table[slot].value (&DAT_00345a34 + slot*0x24) = value/DAT_00352cd4
 *
 * The 4-entry table appears to track minimap markers / scene anchors.
 */

extern unsigned char DAT_00345a38[];
extern float         DAT_00345a34[];
extern float         DAT_00352cd4;
extern void script_eval_expression(void *out);
extern void script_diagnostic(unsigned int);

int op_0xE6_set_minimap_marker_slot(void) {
    int           slot;
    unsigned char flag;
    int           value;
    script_eval_expression(&slot);
    script_eval_expression(&flag);
    script_eval_expression(&value);
    if (slot > 3) script_diagnostic(0x34d3d0);
    int byte_off = slot * 0x24;
    DAT_00345a38[byte_off]                          = flag;
    *(float *)((char *)DAT_00345a34 + byte_off)     = (float)value / DAT_00352cd4;
    return 0;
}
