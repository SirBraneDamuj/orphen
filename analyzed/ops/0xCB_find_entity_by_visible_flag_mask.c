/*
 * Opcode 0xCB — find_entity_by_visible_flag_mask
 *
 * Original: FUN_00264528
 *
 * Reads one expression `mask` and walks the entity pool (DAT_0058beb0,
 * stride 0xEC, up to 256 entries). Returns the first index whose
 * visibility byte at &DAT_005a96b0+i is > 0, whose entity flags word
 * at +0xC has bit 0 set, and whose flag word at +0x6C ANDs with the
 * mask. On match also stores the entity pointer in DAT_00355044
 * (selected_entity). Returns -1 if no match.
 */

#include <stdint.h>

extern unsigned short DAT_0058beb0[];     /* entity_pool */
extern unsigned char  DAT_005a96b0[];     /* per-pool visibility byte */
extern unsigned short *DAT_00355044;      /* selected_entity */
extern void script_eval_expression(void *out);

int op_0xCB_find_entity_by_visible_flag_mask(void) {
    unsigned int mask[4];
    script_eval_expression(mask);
    unsigned short *entity = &DAT_0058beb0[0];
    int idx = 0;
    while (DAT_005a96b0[idx] < 1
        || (*(unsigned int *)(entity + 6)  & 1) == 0
        || (*(unsigned int *)(entity + 0x36) & mask[0]) == 0) {
        idx++;
        entity += 0xEC / 2;
        if (idx > 0xFF) return -1;
    }
    DAT_00355044 = entity;
    return idx;
}
