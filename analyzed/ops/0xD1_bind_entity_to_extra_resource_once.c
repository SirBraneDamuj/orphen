/*
 * Opcode 0xD1 — bind_entity_to_extra_resource_once
 *
 * Original: FUN_002647d0
 *
 * Args: expr `selector`, ip-relative offset, expr `tag` (short stored in
 * DAT_0058bf70).
 *
 * Reads the resource pointer (offset + DAT_00355058) and selects the
 * entity. If the global single-shot guard DAT_0058bf70 is currently 0,
 * calls FUN_00216128(resource_ptr, entity, lead_entity_at_0x58beb0) and
 * stores `tag` into DAT_0058bf70 so subsequent invocations are skipped.
 */

#include <stdint.h>

extern void  *DAT_00355044;
extern int    DAT_00355058;
extern short  DAT_0058bf70;
extern void   script_eval_expression(void *out);
extern int    script_read_ip_offset(void);
extern void   script_select_entity(int i, void *prev);
extern void   FUN_00216128(int rsrc_ptr, void *entity, void *lead);

int op_0xD1_bind_entity_to_extra_resource_once(void) {
    void *prev = DAT_00355044;
    int   selector;
    short tag;
    script_eval_expression(&selector);
    int rsrc_off = script_read_ip_offset();
    int rsrc_ptr = rsrc_off + DAT_00355058;
    script_eval_expression(&tag);
    script_select_entity(selector, prev);
    if (DAT_0058bf70 == 0) {
        FUN_00216128(rsrc_ptr, DAT_00355044, (void *)0x58beb0);
        DAT_0058bf70 = tag;
    }
    return 0;
}
