/*
 * Opcode 0xA3 — read_coroutine_aux
 *
 * Original: FUN_00261f08
 *
 * Reads one expression (first word = slot index, mod 4) and returns the
 * "auxiliary" word stored in that coroutine slot. The aux word is at
 * offset +8 from the slot base (the structure is {code_ptr, aux, return}).
 */

extern void          script_eval_expression(void *out); /* FUN_0025c258 */
extern unsigned int  coroutine_return_word[];           /* &DAT_00571e48 */

unsigned int op_0xA3_read_coroutine_aux(void) {
    int args[4];
    script_eval_expression(args);
    return coroutine_return_word[(args[0] & 3) * 3];
}
