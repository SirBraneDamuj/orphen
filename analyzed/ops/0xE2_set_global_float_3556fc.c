/*
 * Opcode 0xE2 — set_global_float_3556fc
 *
 * Original: FUN_002650e0
 *
 * Reads one int expression and stores it / DAT_00352cd0 into
 * DAT_003556fc (a tunable timing/scale float).
 */

extern float DAT_003556fc;
extern float DAT_00352cd0;
extern void  script_eval_expression(void *out);

int op_0xE2_set_global_float_3556fc(void) {
    int args[4];
    script_eval_expression(args);
    DAT_003556fc = (float)args[0] / DAT_00352cd0;
    return 0;
}
