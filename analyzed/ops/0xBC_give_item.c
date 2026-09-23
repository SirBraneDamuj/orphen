/*
 * Opcode 0xBC — give_item
 *
 * Original: FUN_00263e30
 *
 * Reads expr `index` and adds one of item `index` to the inventory at
 * DAT_003437b8 (one byte per item id, capped at 99). Returns true while the
 * original count was < 99, i.e. while there was room.
 *
 * This is not an event counter. DAT_003437b8 is the item-count table the
 * Equip screen (game mode 3, FUN_0022F620) moves items in and out of and that
 * FUN_002294D0 zeroes on a new game. Spells are items -- ids 1..0xE are the
 * rows of the spell table at DAT_00324FC8 -- so this opcode is how a spell is
 * learned: each arm of s14_e031's reward ladder runs it with its spell's id,
 * and s01_e024's init runs it 43 times to stock a test inventory.
 */

#include <stdbool.h>

extern unsigned char DAT_003437b8[];
extern void          script_eval_expression(void *out);

bool op_0xBC_give_item(void) {
    int args[4];
    script_eval_expression(args);
    unsigned char prev = DAT_003437b8[args[0]];
    if (prev < 99) DAT_003437b8[args[0]] = prev + 1;
    return prev < 99;
}
