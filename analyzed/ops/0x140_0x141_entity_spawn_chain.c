/*
 * Opcodes 0x140 / 0x141 — entity_spawn_chain (shared FUN_00260578)
 *
 * Reads:
 *   expr  source_index
 *   expr  arg_a              (only for 0x141)
 *   expr  unused             (auStack_50|8 — value isn't forwarded)
 *   expr  count
 *   expr  out_slot
 *   short tag                (uStack_44 from auStack_50|0xC)
 *
 * Selects source entity, then calls FUN_00265e28(tag) to get a new
 * entity pointer. On NULL → diagnostic at 0x34cf88.
 *
 * For 0x140, replaces arg_a with FUN_0020dd78(selected_entity, 6).
 *
 * Then on the new entity:
 *   +0x192 = (selected_entity - 0x58beb0) / 0xEC   (pool index)
 *   +0x004 = 0x19
 *   +0x194 = (byte) arg_a
 *   +0x008 = 0
 *   work_mem[out_slot*4] = (new_entity - 0x58beb0) / 0xEC
 *
 * Finally repeats `count` times: FUN_0020dc38(selected_entity, arg_a++)
 * (allocates `count` follow-up actions tied to the source entity).
 */

#include <stdint.h>

extern short            DAT_00355cd8;
extern unsigned short  *DAT_00355044;
extern int              DAT_00355060;
extern void             script_eval_expression(void *out);
extern void             script_select_entity(unsigned int idx, void *ctx);
extern long             FUN_00265e28(unsigned short tag);
extern void             script_diagnostic(unsigned int msg_addr);
extern int              FUN_0020dd78(void *entity, int kind);
extern void             FUN_0020dc38(void *entity, int slot);

unsigned int op_0x140_0x141_entity_spawn_chain(void) {
    short opcode = DAT_00355cd8;
    void *ctx    = DAT_00355044;
    unsigned int   src_idx;
    int            arg_a = 0;
    unsigned int   ignored;
    int            count;
    int            out_slot;
    unsigned short tag;

    script_eval_expression(&src_idx);
    if (opcode == 0x141) script_eval_expression(&arg_a);
    script_eval_expression(&ignored);
    script_eval_expression(&count);
    script_eval_expression(&out_slot);   /* placement matches stack layout */
    script_eval_expression(&tag);
    /* The raw decompile uses one int slot to back tag, hence the mixed read order. */

    script_select_entity(src_idx, ctx);
    long lp = FUN_00265e28(tag);
    if (lp == 0) {
        script_diagnostic(0x34cf88);
    } else {
        if (opcode == 0x140) arg_a = FUN_0020dd78(DAT_00355044, 6);
        char *ne = (char *)(intptr_t)lp;
        *(short        *)(ne + 0x192) =
            (short)(((intptr_t)DAT_00355044 - 0x58beb0) * -0x5f75270d >> 3);
        *(unsigned short *)(ne + 0x004) = 0x19;
        *(unsigned char  *)(ne + 0x194) = (unsigned char)arg_a;
        *(unsigned short *)(ne + 0x008) = 0;
        *(int *)(out_slot * 4 + DAT_00355060) =
            ((intptr_t)ne - 0x58beb0) * -0x5f75270d >> 3;
        for (int slot = arg_a; count > 0; count--, slot++) {
            FUN_0020dc38(DAT_00355044, slot);
        }
    }
    return 0;
}
