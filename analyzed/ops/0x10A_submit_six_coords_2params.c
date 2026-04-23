/*
 * Opcode 0x10A — submit_six_coords_2params
 *
 * Original: FUN_00262690
 *
 * Reads 8 args (4 coords + a packed pair + 2 coords + 1 raw word) and
 * forwards to FUN_00219fc8 with the six coordinates scaled by
 * DAT_00352c70.
 */

extern float DAT_00352c70;
extern void  script_eval_expression(void *out);
extern void  FUN_00219fc8(float, float, float, float, float, float,
                          unsigned short, unsigned int);

unsigned int op_0x10A_submit_six_coords_2params(void) {
    int   c[6];
    unsigned short pair;
    unsigned int   tail;
    script_eval_expression(&c[0]);
    script_eval_expression(&c[1]);
    script_eval_expression(&c[2]);
    script_eval_expression(&c[3]);
    script_eval_expression(&pair);
    script_eval_expression(&c[4]);
    script_eval_expression(&c[5]);
    script_eval_expression(&tail);
    FUN_00219fc8((float)c[0] / DAT_00352c70, (float)c[1] / DAT_00352c70,
                 (float)c[2] / DAT_00352c70, (float)c[3] / DAT_00352c70,
                 (float)c[4] / DAT_00352c70, (float)c[5] / DAT_00352c70,
                 pair, tail);
    return 0;
}
