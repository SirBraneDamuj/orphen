/*
 * Opcode 0xD9 — register_entity_callback
 *
 * Original: FUN_00264e30
 *
 * Args: expr `selector`, ip-relative offset.
 *
 * Selects the entity (using the previously selected entity as fallback)
 * and arms a deferred callback by writing offset+script_code_base into
 * the entity's +0x1A8 slot, then clearing flag bit 0x10 at +4 and
 * setting bit 0x2000 at +2 (mark callback active).
 */

#include <stdint.h>

extern int  iGpffffb0d4;        /* selected_entity (int alias) */
extern int  iGpffffb0e8;        /* script_code_base */
extern void script_eval_expression(void *out);
extern int  script_read_ip_offset(void);
extern void script_select_entity(int i, int prev);

int op_0xD9_register_entity_callback(void) {
    int prev = iGpffffb0d4;
    int selector;
    script_eval_expression(&selector);
    int ptr = script_read_ip_offset() + iGpffffb0e8;
    script_select_entity(selector, prev);
    *(int *)           (intptr_t)(prev + 0x1A8) = ptr;
    *(unsigned short *)(intptr_t)(prev + 4)    &= 0xFFEF;
    *(unsigned short *)(intptr_t)(prev + 2)    |= 0x2000;
    return 0;
}
