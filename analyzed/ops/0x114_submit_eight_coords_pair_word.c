/*
 * Opcode 0x114 — submit_eight_coords_pair_word
 *
 * Original: FUN_00262dd8
 *
 * Reads 11 args; forwards to FUN_00220f70 with eight coordinates scaled
 * by fGpffff8d20 plus a packed pair and a tail word.
 */

extern float fGpffff8d20;
extern void  script_eval_expression(void *out);
extern void  FUN_00220f70(float, float, float, float, float, float, float,
                          float, unsigned short, unsigned int);

void op_0x114_submit_eight_coords_pair_word(void) {
    unsigned short pair;
    int            c[8];
    unsigned int   ignored;
    unsigned int   tail;
    script_eval_expression(&pair);
    script_eval_expression(&c[0]);
    script_eval_expression(&c[1]);
    script_eval_expression(&c[2]);
    script_eval_expression(&c[3]);
    script_eval_expression(&c[4]);
    script_eval_expression(&c[5]);
    script_eval_expression(&c[6]);
    script_eval_expression(&c[7]);
    script_eval_expression(&ignored);  /* auStack_1c — read but unused */
    script_eval_expression(&tail);
    FUN_00220f70((float)c[0] / fGpffff8d20, (float)c[1] / fGpffff8d20,
                 (float)c[2] / fGpffff8d20, (float)c[3] / fGpffff8d20,
                 (float)c[4] / fGpffff8d20, (float)c[5] / fGpffff8d20,
                 (float)c[6] / fGpffff8d20, (float)c[7] / fGpffff8d20,
                 pair, tail);
}
