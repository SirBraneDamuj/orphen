/*
 * Opcode 0x142 — entity_remove_actions
 *
 * Original: FUN_002606d0
 *
 * Reads entity index, selects entity, calls FUN_00265f70 (likely a
 * cleanup hook) on it, looks up its type via FUN_002298d0; when the
 * lookup is 0 calls FUN_00251e40, then FUN_0020dc48(entity, -1) to
 * remove all attached follow-up actions.
 */

extern unsigned short *DAT_00355044;
extern void            script_eval_expression(void *out);
extern void            script_select_entity(unsigned int idx, void *ctx);
extern void            FUN_00265f70(void *entity);
extern long            FUN_002298d0(unsigned short type);
extern void            FUN_00251e40(void *entity);
extern void            FUN_0020dc48(void *entity, long slot);

unsigned int op_0x142_entity_remove_actions(void) {
    unsigned short *ctx = DAT_00355044;
    unsigned int    idx[4];
    script_eval_expression(idx);
    script_select_entity(idx[0], ctx);
    FUN_00265f70(DAT_00355044);
    if (FUN_002298d0(*DAT_00355044) == 0) FUN_00251e40(DAT_00355044);
    FUN_0020dc48(DAT_00355044, -1);
    return 0;
}
