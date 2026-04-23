/*
 * Opcode 0xB0 — swap_lead_with_selected
 *
 * Original: FUN_00263630
 *
 * Reads one expression (entity selector). Resolves the selected entity to
 * a "kind" via entity_lookup_by_type. If the selected entity already is
 * the lead (selected == &DAT_0058beb0) returns 0.
 *
 * Otherwise:
 *   - destroys the previous "selected-equip" object at 0x58c610;
 *   - swaps 0x1D8 bytes of state between lead (DAT_0058beb0) and selected;
 *   - rewrites the new selected (= old lead) flag bits and stashes its old
 *     transform fields into DAT_0058bf24/28/2C/30;
 *   - rewrites the new lead's flag bits + sets DAT_0058beb2 = 1;
 *   - normalizes the party_slots table for both kinds, clearing the new
 *     lead's slot flag and setting the displaced lead's;
 *   - if escort flag 0x507 is set and DAT_00343782 is in range, rebinds
 *     that escort's owner pointer to whichever entity is now of type 1;
 *   - hides both entities, then re-shows the new lead.
 *
 * Returns 1 on a successful swap.
 */

#include <stdint.h>

extern short *selected_entity;                  /* DAT_00355044 */
extern short  entity_pool[];                    /* DAT_0058beb0 */
extern short  party_slots[];                    /* DAT_00343692 */
extern unsigned char DAT_00343782;
extern unsigned char DAT_0058c076[];
extern short DAT_0058beb0, DAT_0058beb2;
extern unsigned int DAT_0058beb4, DAT_0058beb8;
extern unsigned int DAT_0058bf24, DAT_0058bf28, DAT_0058bf2c, DAT_0058bf30;

extern void script_eval_expression(void *out);  /* FUN_0025c258 */
extern void script_select_entity(int i, void *prev); /* FUN_0025d6c0 */
extern int  entity_lookup_by_type(int type_word);    /* FUN_002298d0 */
extern void destroy_object(void *ptr);          /* FUN_00265ec0 */
extern void memcopy_entity(void *dst, void *src, int n); /* FUN_00267da0 */
extern long flag_is_set(int flag_id);           /* FUN_00266368 */
extern void flag_clear(int flag_id);            /* FUN_002663a0 */
extern void flag_set(int flag_id);              /* FUN_002663d8 */
extern void entity_hide(void *entity);          /* FUN_00251e40 */
extern void entity_show(void *entity);          /* FUN_00251dc0 */
extern void script_diagnostic(unsigned int);    /* FUN_0026bfc0 */

int op_0xB0_swap_lead_with_selected(void) {
    short *prev_selected = selected_entity;
    int    selector;
    script_eval_expression(&selector);
    script_select_entity(selector, prev_selected);
    if (*selected_entity == 0x38) *selected_entity = selected_entity[0xE7];

    long selected_kind = entity_lookup_by_type(*selected_entity);
    if (selected_kind > 6) script_diagnostic(0x34d280);
    if (selected_entity == &DAT_0058beb0) return 0;

    unsigned char backup[480];
    destroy_object((void *)0x58c610);
    memcopy_entity(backup, &DAT_0058beb0, 0x1D8);
    memcopy_entity(&DAT_0058beb0, selected_entity, 0x1D8);
    memcopy_entity(selected_entity, backup, 0x1D8);

    short *se = selected_entity;
    unsigned short sf2 = (unsigned short)se[2];
    se[1] = 0x4004;
    se[4] = (short)(((unsigned short)se[4] & 0xFFFB) | 0x12);
    DAT_0058bf24 = *(unsigned int *)(se + 0x3A);
    se[2]   = (short)((sf2 & 0xDFEE) | 0x80);
    se[0x50] = 1;
    DAT_0058bf2c = *(unsigned int *)(se + 0x3E);
    DAT_0058bf30 = *(unsigned int *)(se + 0x40);
    DAT_0058bf28 = *(unsigned int *)(se + 0x3C);

    DAT_0058beb8 |= 0x14;
    DAT_0058beb4 = (DAT_0058beb4 & 0xFFEE) | 0x3000;
    DAT_0058beb2 = 1;

    int new_lead_kind = entity_lookup_by_type(DAT_0058beb0);
    party_slots[new_lead_kind * 0x14 / 2] = 0;
    flag_clear(new_lead_kind + 0x501);

    long selected_kind2 = entity_lookup_by_type(*selected_entity);
    if (selected_kind2 < 7) {
        party_slots[selected_kind2 * 0x14 / 2] = 0x100;
        selected_entity[0x30] = 0;
        flag_set((int)selected_kind2 + 0x501);
    }

    if (flag_is_set(0x507) != 0 && DAT_00343782 < 0x100) {
        int escort_idx = (int)(short)DAT_00343782;
        short **owner_slot = (short **)(&DAT_0058c076 + escort_idx * 0x1D8 - 0x2A);
        if (entity_lookup_by_type(DAT_0058beb0) == 1) {
            *owner_slot = &DAT_0058beb0;
        } else if (entity_lookup_by_type(*selected_entity) == 1) {
            *owner_slot = selected_entity;
        }
    }

    entity_hide(&DAT_0058beb0);
    entity_hide(selected_entity);
    entity_show(&DAT_0058beb0);
    return 1;
}
