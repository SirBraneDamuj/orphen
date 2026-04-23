/*
 * Opcode 0xB6 — clear_entity_anim_slot
 *
 * Original: FUN_00263c10
 *
 * Args: expr `selector`, expr `anim_slot`. Selects the entity and calls
 * anim_stop_slot (FUN_0020dd78) to cancel that animation slot.
 */

#include <stdint.h>

extern unsigned int uGpffffb0d4;
extern void script_eval_expression(void *out);
extern void script_select_entity(int i, void *prev);
extern void anim_stop_slot(unsigned int entity_u, int anim_slot);

void op_0xB6_clear_entity_anim_slot(void) {
    int selector, anim_slot;
    script_eval_expression(&selector);
    script_eval_expression(&anim_slot);
    script_select_entity(selector, (void *)(intptr_t)uGpffffb0d4);
    anim_stop_slot(uGpffffb0d4, anim_slot);
}
