/*
 * Opcode 0x10B — submit_seven_coords_3params
 *
 * Original: FUN_00262780
 *
 * Reads 10 args (4 coords, packed pair, 3 coords, 2 raw words) and
 * forwards to FUN_002198a0 with seven coordinates scaled by
 * DAT_00352c74.
 */

extern float DAT_00352c74;
extern void  script_eval_expression(void *out);
extern void  FUN_002198a0(float, float, float, float, float, float, float,
                          unsigned short, unsigned int, unsigned int);

unsigned int op_0x10B_submit_seven_coords_3params(void) {
    int            c[7];
    unsigned short pair;
    unsigned int   p0, p1;
    script_eval_expression(&c[0]);
    script_eval_expression(&c[1]);
    script_eval_expression(&c[2]);
    script_eval_expression(&c[3]);
    script_eval_expression(&pair);
    script_eval_expression(&c[4]);
    script_eval_expression(&c[5]);
    script_eval_expression(&c[6]);
    script_eval_expression(&p0);
    script_eval_expression(&p1);
    FUN_002198a0((float)c[0] / DAT_00352c74, (float)c[1] / DAT_00352c74,
                 (float)c[2] / DAT_00352c74, (float)c[3] / DAT_00352c74,
                 (float)c[4] / DAT_00352c74, (float)c[5] / DAT_00352c74,
                 (float)c[6] / DAT_00352c74, pair, p0, p1);
    return 0;
}
