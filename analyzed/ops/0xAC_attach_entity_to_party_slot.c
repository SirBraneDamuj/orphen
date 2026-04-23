/*
 * Opcode 0xAC — attach_entity_to_party_slot
 *
 * Original: FUN_002631f0
 *
 * Args (4 expressions):
 *   args[0] slot         (0..6; >6 → diag 0x34d1c8)
 *   args[1..2]           ignored / reserved
 *   args[3] selector     (entity selector; 0 → diag 0x34d1f0; <0 → detach)
 *
 * Behaviour:
 *   1. If the slot is currently occupied (flag 0x501+slot is set), the
 *      slot's stored pool index is normalized: 0x100 → 0xFFFF (clear).
 *      Function then exits.
 *   2. Otherwise selects the entity referenced by args[3] and counts how
 *      many of the other six slots are also occupied. If three or more
 *      are filled the operation is silently dropped (party is full).
 *   3. If args[3] < 0 just clears the slot (flag + slot table entry).
 *   4. Otherwise: snapshot the entity type into +0x1A0; pick a sub-index
 *      0..2 not used by any other party member (matched by the byte at
 *      DAT_0058c076[idx*0x1d8]); rewrite the entity into a type-0x37
 *      party member with anim 0xB4, store its slot number at +0x1c7,
 *      tweak entity flag bits, then call entity_init_party_member to
 *      finalize. The slot table records the new pool index, and the slot
 *      flag is cleared (slot is now valid).
 */

#include <stdbool.h>
#include <stdint.h>

extern short *selected_entity;                   /* DAT_00355044 */
extern short  entity_pool[];                     /* DAT_0058beb0 */
extern short  party_slots[];                     /* DAT_00343692, stride 0x14/2 */
extern unsigned char DAT_00343688[];             /* base for entity_init_party_member arg */
extern unsigned char DAT_0058c076[];             /* per-pool sub-index byte (+0xC0) */

extern void  script_eval_expression(void *out);  /* FUN_0025c258 */
extern void  script_diagnostic(unsigned int);    /* FUN_0026bfc0 */
extern void  script_select_entity(int idx, void *prev); /* FUN_0025d6c0 */
extern long  flag_is_set(int flag_id);           /* FUN_00266368 */
extern void  flag_clear(int flag_id);            /* FUN_002663a0 */
extern void  entity_init_party_member(void *entity, void *table_entry); /* FUN_0023a518 */

int op_0xAC_attach_entity_to_party_slot(void) {
    int   args[4];
    short *prev_selected = selected_entity;
    script_eval_expression(&args[0]);
    script_eval_expression(&args[1]);
    script_eval_expression(&args[2]);
    script_eval_expression(&args[3]);
    int slot     = args[0];
    int selector = args[3];
    if (slot > 6)      script_diagnostic(0x34d1c8);
    if (selector == 0) script_diagnostic(0x34d1f0);

    if (flag_is_set(slot + 0x501) != 0) {
        if (party_slots[slot * 0x14 / 2] == 0x100) {
            party_slots[slot * 0x14 / 2] = (short)0xFFFF;
        }
        return 0;
    }

    script_select_entity(selector, prev_selected);

    int occupied = 0;
    for (int i = 0; i < 7; i++) {
        if (i == 6) continue;                            /* matches original */
        if (flag_is_set(i + 0x501) != 0) occupied++;
    }
    if (occupied >= 3) return 0;

    if (selector < 0) {
        flag_clear(slot + 0x501);
        party_slots[slot * 0x14 / 2] = (short)0xFFFF;
        return 0;
    }

    /* Stash original type so it can be restored on swap-out. */
    if (*selected_entity == 0x38) {
        selected_entity[0xD0] = selected_entity[0xE7];
    } else {
        selected_entity[0xD0] = *selected_entity;
    }

    /* Find an unused sub-index 0..2. */
    int sub_index = 0;
    bool collision;
    do {
        collision = false;
        for (int i = 0; i < 7; i++) {
            short bound = party_slots[i * 0x14 / 2];
            if (bound != 0 && bound < 0x100 &&
                (signed char)DAT_0058c076[bound * 0x1D8] == sub_index) {
                collision = true;
                break;
            }
        }
        if (!collision) break;
        sub_index++;
    } while (sub_index < 3);
    unsigned char assigned_sub = collision ? 3 : (unsigned char)sub_index;

    /* Rewrite the entity as a type-0x37 party member. */
    *(unsigned char *)(selected_entity + 0xE3) = assigned_sub;
    selected_entity[0xD1] = 0xB4;
    *((unsigned char *)selected_entity + 0x1C7) = (unsigned char)slot;

    unsigned short f1 = (unsigned short)selected_entity[1];
    unsigned short f2 = (unsigned short)selected_entity[2];
    *(unsigned int *)(selected_entity + 0x3A) |= 0x0D000000;
    *selected_entity   = 0x37;
    selected_entity[1] = (short)((f1 & 0xBFFF) | 0x1002);
    selected_entity[2] = (short)((f2 & 0xFFEE) | 0x2000);
    selected_entity[0x30] = 0;

    entity_init_party_member(selected_entity, &DAT_00343688[slot * 0x28]);

    long entity_index =
        ((intptr_t)(selected_entity - (short *)&entity_pool[0])) /
        (long)(0xEC / sizeof(short));
    party_slots[slot * 0x14 / 2] = (short)entity_index;
    flag_clear(slot + 0x501);
    return 0;
}
