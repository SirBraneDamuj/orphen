/*
 * Opcode 0xB7 — set_entity_short_and_word
 *
 * Original: FUN_00263c58
 *
 * Args: expr `selector`, expr `value_u16` (low 16 bits), expr `value_u32`.
 * Selects the entity, then writes:
 *   entity[+0x130] = value_u16 (short)
 *   entity[+0x198] = value_u32 (word)
 * Used to stash short/word target/marker values on an entity from script.
 */

#include <stdint.h>

extern int  selected_entity_as_int;
extern void script_eval_expression(void *out);
extern void script_select_entity(int i, void *prev);

int op_0xB7_set_entity_short_and_word(void) {
    int          selector;
    short        value_u16;
    unsigned int value_u32;
    int          prev_entity = selected_entity_as_int;
    script_eval_expression(&selector);
    script_eval_expression(&value_u16);
    script_eval_expression(&value_u32);
    script_select_entity(selector, (void *)(intptr_t)prev_entity);
    *(short *)       ((intptr_t)selected_entity_as_int + 0x130) = value_u16;
    *(unsigned int *)((intptr_t)selected_entity_as_int + 0x198) = value_u32;
    return 0;
}
