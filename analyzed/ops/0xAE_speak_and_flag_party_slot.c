/*
 * Opcode 0xAE — speak_and_flag_party_slot
 *
 * Original: FUN_00263518
 *
 * Args: expr `slot` (>6 → diag 0x34d238), expr `set_flag`. Behaves like
 * 0xAD (entity_speak), and additionally sets the per-entity dialogue-
 * consumed marker at pool[idx]+0x60 (DAT_0058bf10 + idx*0xEC) when
 * `set_flag` is non-zero.
 */

#include <stdint.h>

extern short party_slots[];                        /* DAT_00343692 */
extern short entity_pool[];                        /* DAT_0058beb0 */
extern unsigned char DAT_0058bf10[];               /* lead +0x60 — pool stride 0xEC */
extern void  script_eval_expression(void *out);    /* FUN_0025c258 */
extern void  script_diagnostic(unsigned int);      /* FUN_0026bfc0 */
extern void  entity_speak(void *entity, short slot);/* FUN_002589c0 */

int op_0xAE_speak_and_flag_party_slot(void) {
    int slot, set_flag;
    script_eval_expression(&slot);
    script_eval_expression(&set_flag);
    if (slot > 6) script_diagnostic(0x34d238);
    short pool_idx = party_slots[slot * 0x14 / 2];
    if (pool_idx != 0 && pool_idx < 0x100) {
        entity_speak(&entity_pool[pool_idx * 0xEC / 2], (short)slot);
        if (set_flag != 0) DAT_0058bf10[pool_idx * 0xEC] = 1;
    }
    return 0;
}
