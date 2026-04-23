/*
 * Opcode 0x9F — slot_is_occupied
 *
 * Original: FUN_00261d88
 *
 * Reads one inline byte (script-IP advanced by 1) as a slot index. If the
 * index is < 0x41 returns whether the corresponding word in the 0x41-entry
 * `script_slot_table` (iGpffffbd84) is non-zero. Out-of-range raises a
 * diagnostic with msg id 0x34d178 and returns false.
 *
 * Used by scripts together with 0xA0 (find_free_slot) to manage their
 * deferred-call slots.
 */

#include <stdbool.h>

extern unsigned char *script_byte_cursor;     /* pbGpffffbd60 */
extern int            script_slot_table[];    /* iGpffffbd84, 0x41 entries */
extern void           script_diagnostic(unsigned int msg); /* FUN_0026bfc0 */

bool op_0x9F_slot_is_occupied(void) {
    unsigned char idx = *script_byte_cursor++;
    if (idx >= 0x41) {
        script_diagnostic(0x34d178);
        return false;
    }
    return script_slot_table[idx] != 0;
}
