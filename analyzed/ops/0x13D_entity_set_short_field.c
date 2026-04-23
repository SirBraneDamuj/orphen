/*
 * Opcode 0x13D — entity_set_short_field
 *
 * Original: FUN_00265410
 *
 * Reads entity index + short value. Selects entity, then forwards to
 * FUN_00225bc8(selected_entity, short_value).
 */

extern unsigned short *DAT_00355044;
extern void            script_eval_expression(void *out);
extern void            script_select_entity(unsigned int idx, void *ctx);
extern void            FUN_00225bc8(void *entity, unsigned short v);

unsigned int op_0x13D_entity_set_short_field(void) {
    void          *ctx = DAT_00355044;
    unsigned int   idx;
    unsigned short val;
    script_eval_expression(&idx);
    script_eval_expression(&val);
    script_select_entity(idx, ctx);
    FUN_00225bc8(DAT_00355044, val);
    return 0;
}
