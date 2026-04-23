/*
 * Opcode 0xB1 — bind_entity_action
 *
 * Original: FUN_00263878
 *
 * Args: expr `selector`, expr `action_id`.
 *
 * Selects the chosen entity, calls anim_bind_action(entity, action_id)
 * and ORs the "action triggered" bit (0x10) into entity.flags at +8.
 */

#include <stdint.h>

extern int  selected_entity_as_int;             /* iGpffffb0d4 */
extern void script_eval_expression(void *out);  /* FUN_0025c258 */
extern void script_select_entity(int i, void *prev); /* FUN_0025d6c0 */
extern void anim_bind_action(void *entity, int action); /* FUN_0020d9c8 */

int op_0xB1_bind_entity_action(void) {
    int selector, action;
    int prev_entity = selected_entity_as_int;
    script_eval_expression(&selector);
    script_eval_expression(&action);
    script_select_entity(selector, (void *)(intptr_t)prev_entity);
    anim_bind_action((void *)(intptr_t)selected_entity_as_int, action);
    *(unsigned short *)((intptr_t)selected_entity_as_int + 8) |= 0x10;
    return 0;
}
