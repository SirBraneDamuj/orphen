/*
 * Opcode 0xD9 — set_prop_break_table
 *
 * Original: FUN_00264e30
 *
 * Args: expr `selector`, stream offset.
 *
 * Makes a map-streamed prop breakable. Stores offset + script base in the
 * entity's +0x1A8, clears +0x04 bit 0x10 (the hit tests skip an entity with
 * it) and sets +0x02 bit 0x2000 (a player-side attack's target mask,
 * FUN_002148a8 / FUN_00215670, looks for 0x2008).
 *
 * The offset is NOT script code. An earlier note called this a deferred
 * callback; it is a table of 0x24-byte records ended by a 0xFFFF halfword,
 * read by FUN_002cfe08 (the streamed-prop behaviour):
 *   +0x00 u16  hit-kind bit: the record applies when +0xC2 & (1 << this)
 *   +0x02 u16  debris type FUN_002d0058 spawns   +0x04 s16 debris count
 *   +0x06 s16  state-3 timer, << 5               +0x08 s16 debris scale bias
 *   +0x0C u32  point-light colour, 0 for none    +0x10 u16 -> +0x1AC
 *   +0x12 s16  / 100 -> +0x1B0                   +0x14..+0x1C light offset
 *   +0x20 s32  offset (from +0x20) to the debris positions
 * s03_e001's table at 0x5E70: kind 4, 12 debris of type 0x47.
 *
 * The writes go through the entity selected BEFORE the selector is applied
 * (s0 is loaded from DAT_00355044 first), not the newly selected one.
 */

#include <stdint.h>

extern int  iGpffffb0d4;        /* DAT_00355044, selected entity */
extern int  iGpffffb0e8;        /* script base */
extern void script_eval_expression(void *out);
extern int  script_read_ip_offset(void);
extern void script_select_entity(int i, int prev);

int op_0xD9_set_prop_break_table(void) {
    int prev = iGpffffb0d4;
    int selector;
    script_eval_expression(&selector);
    int table = script_read_ip_offset() + iGpffffb0e8;
    script_select_entity(selector, prev);
    *(int *)           (intptr_t)(prev + 0x1A8) = table;
    *(unsigned short *)(intptr_t)(prev + 4)    &= 0xFFEF;
    *(unsigned short *)(intptr_t)(prev + 2)    |= 0x2000;
    return 0;
}
