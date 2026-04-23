/*
 * Opcode 0xCF — entity_apply_extra_resource
 *
 * Original: FUN_00264740
 *
 * Args: expr `selector`, ip-relative offset (read by script_read_ip_offset).
 * Adds the offset to DAT_00355058 (script_extra_code_base) to produce a
 * resource pointer, selects the entity, and calls FUN_002148a8 with
 * (entity, resource_ptr) — applies a script-relative resource blob to
 * the entity (e.g. an animation/state table).
 */

#include <stdint.h>

extern void *DAT_00355044;
extern int   DAT_00355058;
extern void  script_eval_expression(void *out);
extern int   script_read_ip_offset(void);
extern void  script_select_entity(int i, void *prev);
extern void  FUN_002148a8(void *entity, int resource_ptr);

void op_0xCF_entity_apply_extra_resource(void) {
    void *prev = DAT_00355044;
    int   selector[4];
    script_eval_expression(selector);
    int rsrc_off = script_read_ip_offset();
    int rsrc_ptr = rsrc_off + DAT_00355058;
    script_select_entity(selector[0], prev);
    FUN_002148a8(DAT_00355044, rsrc_ptr);
}
