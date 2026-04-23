/*
 * Opcode 0xAF — select_party_slot_as_current
 *
 * Original: FUN_002635c0
 *
 * Reads one expression `slot` (>6 → diag 0x34d258), translates it via
 * party_slots to a pool index and points DAT_00355044 (selected_entity)
 * at the corresponding entity. Subsequent opcodes that operate on the
 * "current" entity will then act on the chosen party member.
 */

extern short *selected_entity;                  /* DAT_00355044 */
extern short  party_slots[];                    /* DAT_00343692 */
extern short  entity_pool[];                    /* DAT_0058beb0 */
extern void   script_eval_expression(void *out);/* FUN_0025c258 */
extern void   script_diagnostic(unsigned int);  /* FUN_0026bfc0 */

void op_0xAF_select_party_slot_as_current(void) {
    int args[4];
    script_eval_expression(args);
    if (args[0] > 6) script_diagnostic(0x34d258);
    short idx = party_slots[args[0] * 0x14 / 2];
    selected_entity = &entity_pool[idx * 0xEC / 2];
}
