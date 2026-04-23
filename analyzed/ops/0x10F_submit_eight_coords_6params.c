/*
 * Opcode 0x10F — submit_eight_coords_6params
 *
 * Original: FUN_00262b90
 *
 * Reads 14 args; forwards to FUN_0021ed50 with eight coords scaled by
 * fGpffff8d14, a leading raw word, a packed pair, three bytes and
 * a tail word.
 */

extern float fGpffff8d14;
extern void  script_eval_expression(void *out);
extern void  FUN_0021ed50(float, float, float, float, float, float, float,
                          float, unsigned int, unsigned short,
                          unsigned char, unsigned char, unsigned int);

unsigned int op_0x10F_submit_eight_coords_6params(void) {
    unsigned int   head;
    int            c[8];
    unsigned short pair;
    unsigned char  ignored, b0, b1;
    unsigned int   tail;
    script_eval_expression(&head);
    script_eval_expression(&c[0]);
    script_eval_expression(&c[1]);
    script_eval_expression(&c[2]);
    script_eval_expression(&c[3]);
    script_eval_expression(&c[4]);
    script_eval_expression(&c[5]);
    script_eval_expression(&pair);
    script_eval_expression(&c[6]);
    script_eval_expression(&c[7]);
    script_eval_expression(&ignored);   /* auStack_28 — used only for size; not forwarded */
    script_eval_expression(&b0);
    script_eval_expression(&b1);
    script_eval_expression(&tail);
    FUN_0021ed50((float)c[0] / fGpffff8d14, (float)c[1] / fGpffff8d14,
                 (float)c[2] / fGpffff8d14, (float)c[3] / fGpffff8d14,
                 (float)c[4] / fGpffff8d14, (float)c[5] / fGpffff8d14,
                 (float)c[6] / fGpffff8d14, (float)c[7] / fGpffff8d14,
                 head, pair, b0, b1, tail);
    return 0;
}
