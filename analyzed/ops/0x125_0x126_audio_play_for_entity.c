/*
 * Opcodes 0x125 / 0x126 — audio_play_for_entity (shared FUN_00261330)
 *
 * Reads one inline 16-bit script word as the audio tag, advances the
 * cursor by 2, then reads an entity index expression and selects it.
 *  - 0x125: FUN_00267d38(tag) — fire-and-forget for selected entity.
 *  - 0x126: read another expression as `flags`, then
 *           FUN_00267d88(tag, selected_entity, flags).
 */

#include <stdint.h>

extern short            DAT_00355cd8;        /* current opcode */
extern unsigned char   *DAT_00355cd0;        /* script byte cursor */
extern unsigned short  *DAT_00355044;        /* selected entity */
extern void             script_eval_expression(void *out);
extern void             script_select_entity(unsigned int idx, void *ctx);
extern void             FUN_00267d38(int tag);
extern void             FUN_00267d88(int tag, void *entity, unsigned int flags);

unsigned int op_0x125_0x126_audio_play_for_entity(void) {
    short          opcode = DAT_00355cd8;
    void          *ctx    = DAT_00355044;
    int            tag    = (int)((((unsigned int)DAT_00355cd0[0]
                                  + (unsigned int)DAT_00355cd0[1] * 0x100) << 16)) >> 16;
    DAT_00355cd0 += 2;
    unsigned int   idx;
    script_eval_expression(&idx);
    script_select_entity(idx, ctx);
    if (opcode == 0x125) {
        FUN_00267d38(tag);
    } else {
        unsigned int flags;
        script_eval_expression(&flags);
        FUN_00267d88(tag, DAT_00355044, flags);
    }
    return 0;
}
