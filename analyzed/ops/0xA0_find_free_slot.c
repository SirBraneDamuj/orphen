/*
 * Opcode 0xA0 — find_free_slot
 *
 * Original: FUN_00261de0
 *
 * Linearly scans the 0x3E-entry `script_slot_table` (iGpffffbd84) and
 * returns the index of the first slot whose word is zero. If none is free
 * raises diagnostic msg id 0x34d188 and returns -1.
 *
 * Pairs with 0x9F/0xA1/0xA2 (slot occupancy / set / clear).
 */

extern int  script_slot_table[];                /* iGpffffbd84 */
extern void script_diagnostic(unsigned int msg);/* FUN_0026bfc0 */

int op_0xA0_find_free_slot(void) {
    for (int i = 0; i < 0x3E; i++) {
        if (script_slot_table[i] == 0) return i;
    }
    script_diagnostic(0x34d188);
    return -1;
}
