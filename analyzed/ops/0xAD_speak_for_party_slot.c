/*
 * Opcode 0xAD — speak_for_party_slot
 *
 * Original: FUN_00263498
 *
 * Reads one expression `slot` (>6 → diag 0x34d218). Looks up the bound
 * pool index in party_slots. If the index is in range (>0 and <0x100),
 * triggers the slot's spoken line via entity_speak (FUN_002589c0) on the
 * pooled entity.
 */

#include <stdint.h>

extern short party_slots[];      /* DAT_00343692, stride 0x14/2 */
extern short entity_pool[];      /* DAT_0058beb0, stride 0xEC */
extern void  script_eval_expression(void *out); /* FUN_0025c258 */
extern void  script_diagnostic(unsigned int);   /* FUN_0026bfc0 */
extern void  entity_speak(void *entity, short slot); /* FUN_002589c0 */

int op_0xAD_speak_for_party_slot(void) {
    int args[4];
    script_eval_expression(args);
    if (args[0] > 6) script_diagnostic(0x34d218);
    short pool_idx = party_slots[args[0] * 0x14 / 2];
    if (pool_idx != 0 && pool_idx < 0x100) {
        entity_speak(&entity_pool[pool_idx * 0xEC / 2], (short)args[0]);
    }
    return 0;
}
