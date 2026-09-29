/*
 * Opcode 0xB0 — swap_lead_with_selected
 *
 * Original: FUN_00263630
 *
 * Hands control to another party character. The lead player is pool slot 0
 * (DAT_0058beb0), not a pointer, so this trades the two slots' 0x1D8 bytes
 * outright and then patches both halves. s03_e001 uses it to give the player
 * Sephy for the floating-logs section; the s01_e024 debug room uses it on
 * every party member ("player change").
 *
 * Reads one expression (entity selector). A type 0x38 (opcode 0x66 NPC) gets
 * its real type back from +0x1CE first.
 *
 * Early-out: the selected entity has the **same type** as the lead. This is a
 * type comparison, not a pointer one (0x2636a8..0x2636b4 load both +0x00
 * halfwords and bne on them).
 *
 * Otherwise:
 *   - FUN_00265ec0(slot 4): destroys the bandana (cascading release);
 *   - swaps 0x1D8 bytes between lead and selected through a stack buffer;
 *   - selected (= old lead): +0x02 = 0x4004, +0x04 = (|0x80) & 0xDFEE,
 *     +0x08 = (|0x12) & 0xFFFB, +0xA0 = 1;
 *   - lead +0x74..+0x83 (terrain masks, step-down, slope limit) are copied
 *     back from selected, so those four words stay with slot 0;
 *   - lead: +0x04 = (|0x3000) & 0xFFEE, +0x08 |= 0x14, +0x02 = 1;
 *   - party roster (DAT_00343688, 0x28-byte records; DAT_00343692 is +0x0A):
 *     new lead's class -> +0x0A = 0, flag 0x501+class SET (FUN_002663a0);
 *     displaced class (< 7) -> +0x0A = 0x100, +0x60 = 0,
 *     flag 0x501+class CLEARED (FUN_002663d8);
 *   - if flag 0x507 and party slot 6's +0x0A < 0x100: that entity's +0x19C
 *     is set to whichever of lead/selected is **class 1** (type 3), not
 *     type 1;
 *   - FUN_00251e40(lead), FUN_00251e40(selected): each rebuilds the bandana
 *     on its argument if that entity is type 1 — so it follows Orphen;
 *   - FUN_00251dc0(lead): loads the new lead's HP/attack/defence into
 *     +0x128..+0x12E from its party record.
 *
 * Returns 1 on a successful swap.
 *
 * Earlier versions of this note had FUN_002663a0/FUN_002663d8 backwards, a
 * pointer early-out, a 0x14-byte roster stride, "type 1" for the 0x507
 * rebind, and FUN_00251e40/FUN_00251dc0 as hide/show. All five disagree with
 * the disassembly.
 */

#include <stdint.h>

extern short *selected_entity;                  /* DAT_00355044 */
extern short  DAT_0058beb0;                     /* pool slot 0, the lead */
extern short  party_slots[];                    /* DAT_00343692, stride 0x14 shorts */
extern unsigned short DAT_00343782;             /* party slot 6's +0x0A */

extern void script_eval_expression(void *out);  /* FUN_0025c258 */
extern void script_select_entity(int i, void *prev); /* FUN_0025d6c0 */
extern int  character_class(int type_word);     /* FUN_002298d0 */
extern void destroy_entity(void *entity);       /* FUN_00265ec0 */
extern void memcopy(void *dst, void *src, int n); /* FUN_00267da0 */
extern long flag_is_set(int flag_id);           /* FUN_00266368 */
extern void flag_set(int flag_id);              /* FUN_002663a0 */
extern void flag_clear(int flag_id);            /* FUN_002663d8 */
extern void attach_bandana(void *entity);       /* FUN_00251e40 */
extern void load_player_stats(void *entity);    /* FUN_00251dc0 */
extern void script_diagnostic(unsigned int);    /* FUN_0026bfc0 */

int op_0xB0_swap_lead_with_selected(void) {
    short *prev_selected = selected_entity;
    int    selector;
    script_eval_expression(&selector);
    script_select_entity(selector, prev_selected);
    if (*selected_entity == 0x38) *selected_entity = selected_entity[0xE7];

    if (character_class(*selected_entity) > 6) script_diagnostic(0x34d280);
    if (*selected_entity == DAT_0058beb0) return 0;

    short *lead = &DAT_0058beb0;
    unsigned char backup[0x1D8];
    destroy_entity((char *)lead + 4 * 0x1D8);
    memcopy(backup, lead, 0x1D8);
    memcopy(lead, selected_entity, 0x1D8);
    memcopy(selected_entity, backup, 0x1D8);

    short *se = selected_entity;
    se[1] = 0x4004;
    se[4] = (short)(((unsigned short)se[4] | 0x12) & 0xFFFB);
    se[2] = (short)(((unsigned short)se[2] | 0x80) & 0xDFEE);
    se[0x50] = 1;
    *(uint32_t *)(lead + 0x3A) = *(uint32_t *)(se + 0x3A);  /* +0x74 */
    *(uint32_t *)(lead + 0x3E) = *(uint32_t *)(se + 0x3E);  /* +0x7C */
    *(uint32_t *)(lead + 0x40) = *(uint32_t *)(se + 0x40);  /* +0x80 */
    *(uint32_t *)(lead + 0x3C) = *(uint32_t *)(se + 0x3C);  /* +0x78 */
    lead[4] = (short)((unsigned short)lead[4] | 0x14);
    lead[2] = (short)(((unsigned short)lead[2] | 0x3000) & 0xFFEE);
    lead[1] = 1;

    int lead_class = character_class(*lead);
    party_slots[lead_class * 0x14] = 0;
    flag_set(lead_class + 0x501);

    int sel_class = character_class(*selected_entity);
    if (sel_class < 7) {
        party_slots[sel_class * 0x14] = 0x100;
        selected_entity[0x30] = 0;
        flag_clear(sel_class + 0x501);
    }

    if (flag_is_set(0x507) != 0 && DAT_00343782 < 0x100) {
        char *escort = (char *)lead + (short)DAT_00343782 * 0x1D8;
        if (character_class(*lead) == 1) {
            *(short **)(escort + 0x19C) = lead;
        } else if (character_class(*selected_entity) == 1) {
            *(short **)(escort + 0x19C) = selected_entity;
        }
    }

    attach_bandana(lead);
    attach_bandana(selected_entity);
    load_player_stats(lead);
    return 1;
}
