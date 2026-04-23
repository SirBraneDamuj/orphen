/*
 * Opcode 0xBC — increment_event_counter_capped
 *
 * Original: FUN_00263e30
 *
 * Reads expr `index`, takes the byte at &DAT_003437b8[index], increments
 * it (capped at 99) and returns true while the original value was < 99
 * (i.e. while the slot still had room). Used as a counter / one-shot
 * gate by event scripts.
 */

#include <stdbool.h>

extern unsigned char DAT_003437b8[];
extern void          script_eval_expression(void *out);

bool op_0xBC_increment_event_counter_capped(void) {
    int args[4];
    script_eval_expression(args);
    unsigned char prev = DAT_003437b8[args[0]];
    if (prev < 99) DAT_003437b8[args[0]] = prev + 1;
    return prev < 99;
}
