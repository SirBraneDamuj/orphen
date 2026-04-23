/*
 * Opcode 0xEC — set_npc_step_byte
 *
 * Original: FUN_00265880
 *
 * Reads one expression and writes its low byte into focus_npc[+0x1BC]
 * (the step / phase counter byte). Returns the original expression value.
 */

#include <stdint.h>

extern int  iGpffffb0d8;        /* int alias of puGpffffb0d8 */
extern void script_eval_expression(void *out);

unsigned int op_0xEC_set_npc_step_byte(void) {
    unsigned int args[4];
    script_eval_expression(args);
    *(unsigned char *)(intptr_t)(iGpffffb0d8 + 0x1BC) = (unsigned char)args[0];
    return args[0];
}
