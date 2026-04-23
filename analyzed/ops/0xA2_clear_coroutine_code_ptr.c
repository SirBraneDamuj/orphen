/*
 * Opcode 0xA2 — clear_coroutine_code_ptr
 *
 * Original: FUN_00261ea8
 *
 * Reads one expression (first word = slot index, mod 4) and clears the
 * code-pointer word of that coroutine slot at coroutine_code_ptr[slot*3].
 * Disarms a slot armed by 0xA1.
 */

extern void  script_eval_expression(void *out);  /* FUN_0025c258 */
extern int   coroutine_code_ptr[];               /* &DAT_00571e40 */

int op_0xA2_clear_coroutine_code_ptr(void) {
    int args[4];
    script_eval_expression(args);
    coroutine_code_ptr[(args[0] & 3) * 3] = 0;
    return 0;
}
