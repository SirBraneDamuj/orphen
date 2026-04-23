/*
 * Opcode 0xA8 — register_lead_boot_handler
 *
 * Original: FUN_00262f38
 *
 * Reads an IP-relative offset and stores the resolved code pointer into
 * `script_slot_table[0x40]` (the slot reserved for the lead-character boot
 * vector). Then re-initializes the lead character entity at DAT_0058beb0
 * via entity_spawn_typed(lead, kind=10, subkind=1).
 */

#include <stdint.h>

extern int  script_code_base;          /* iGpffffb0e8 */
extern int  script_slot_table_bytes;   /* iGpffffbd84 (used as base+0x100) */
extern int  script_read_ip_offset(void);                /* FUN_0025c1d0 */
extern void entity_spawn_typed(void *entity, int kind, int subkind); /* FUN_00225bf0 */
extern short DAT_0058beb0;            /* lead-character entity */

int op_0xA8_register_lead_boot_handler(void) {
    int ip_rel = script_read_ip_offset();
    *(int *)(script_slot_table_bytes + 0x100) = script_code_base + ip_rel;
    entity_spawn_typed(&DAT_0058beb0, 10, 1);
    return 0;
}
