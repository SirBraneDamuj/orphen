/*
 * Opcode 0x13B — set_global_byte_355657_clamped
 *
 * Original: FUN_002653a0
 *
 * Reads one expr; if > 6, emits diagnostic at 0x34d3f8 ("value out of
 * range"). Stores the low byte into DAT_00355657.
 */

extern unsigned char DAT_00355657;
extern void          script_eval_expression(void *out);
extern void          script_diagnostic(unsigned int msg_addr);

unsigned int op_0x13B_set_global_byte_355657_clamped(void) {
    unsigned int v[4];
    script_eval_expression(v);
    if (v[0] > 6) script_diagnostic(0x34d3f8);
    DAT_00355657 = (unsigned char)v[0];
    return 0;
}
