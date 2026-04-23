/*
 * Opcode 0xC6 — copy_entity_position_to_light
 *
 * Original: FUN_00264360
 *
 * Args: expr `slot` (>0xF → diag 0x34d330), expr `selector`. Selects the
 * entity and copies its world position (+0x20..+0x28) into
 * light_table[slot] xyz floats.
 */

#include <stdint.h>

extern int   *DAT_00355044;       /* selected_entity */
extern float DAT_00343888[], DAT_0034388c[], DAT_00343890[];
extern void  script_eval_expression(void *out);
extern void  script_select_entity(int i, void *prev);
extern void  script_diagnostic(unsigned int);

int op_0xC6_copy_entity_position_to_light(void) {
    void *prev_entity = DAT_00355044;
    unsigned int slot;
    int          selector;
    script_eval_expression(&slot);
    script_eval_expression(&selector);
    script_select_entity(selector, prev_entity);
    if (slot > 0xF) script_diagnostic(0x34d330);
    int *e = DAT_00355044;
    DAT_00343888[slot * 5] = *(float *)((intptr_t)e + 0x20);
    DAT_0034388c[slot * 5] = *(float *)((intptr_t)e + 0x24);
    DAT_00343890[slot * 5] = *(float *)((intptr_t)e + 0x28);
    return 0;
}
