/*
 * Opcode 0xA1 — set_coroutine_code_ptr
 *
 * Original: FUN_00261e30
 *
 * Reads one expression — its first word is a coroutine-slot index (taken
 * mod 4) — then reads a script-IP-relative offset and stores
 *   coroutine_code_ptr[slot]  = ip_offset + script_code_base
 *   coroutine_aux_word[slot]  = 0
 * The coroutine table lives at DAT_00571e40 with stride 0xC; this opcode
 * arms one entry to be invoked later.
 */

#include <stdint.h>

extern void  script_eval_expression(void *out);   /* FUN_0025c258 */
extern int   script_read_ip_offset(void);         /* FUN_0025c1d0 */
extern int   script_code_base;                    /* iGpffffb0e8 */
extern int   coroutine_code_ptr[];                /* &DAT_00571e40, stride 3 ints */
extern int   coroutine_aux_word[];                /* &DAT_00571e44, stride 3 ints */

int op_0xA1_set_coroutine_code_ptr(void) {
    int args[4];
    script_eval_expression(args);
    int ip_rel = script_read_ip_offset();
    int slot   = args[0] & 3;
    coroutine_code_ptr[slot * 3] = ip_rel + script_code_base;
    coroutine_aux_word[slot * 3] = 0;
    return 0;
}
