/*
 * Opcodes 0xE7 / 0xE8 — get_or_set_global_short_355064 (shared handler)
 *
 * Original: FUN_00265290 (DAT_00355cd8 = current opcode id)
 *
 *   0xE8: reads one expression and writes its low 16 bits into
 *         DAT_00355064; returns the new value.
 *   0xE7 (and any other id): no read; returns the current
 *         DAT_00355064 as a u32.
 *
 * Effectively a get/set pair sharing one handler.
 */

extern unsigned char  DAT_00355cd8;     /* current_opcode_id alias */
extern unsigned short DAT_00355064;
extern void           script_eval_expression(void *out);

unsigned int op_0xE7_0xE8_get_or_set_global_short_355064(void) {
    unsigned int value[4];
    if (DAT_00355cd8 == 0xE8) {
        script_eval_expression(value);
        DAT_00355064 = (unsigned short)value[0];
        return value[0];
    }
    return (unsigned int)DAT_00355064;
}
