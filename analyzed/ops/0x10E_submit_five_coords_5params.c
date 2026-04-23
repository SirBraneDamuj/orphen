/*
 * Opcode 0x10E — submit_five_coords_5params
 *
 * Original: FUN_00262a98
 *
 * Reads 10 args; forwards to FUN_0021f6e8 with five coords scaled by
 * fGpffff8d10 plus a packed pair, a raw short, three raw bytes and
 * a tail word.
 */

extern float fGpffff8d10;
extern void  script_eval_expression(void *out);
extern void  FUN_0021f6e8(float, float, float, float, float,
                          unsigned short, unsigned short,
                          unsigned char, unsigned char, unsigned int);

unsigned int op_0x10E_submit_five_coords_5params(void) {
    unsigned short pair;
    int            c[5];
    unsigned short tail_short;
    unsigned char  b0, b1;
    unsigned int   tail_word;
    script_eval_expression(&pair);
    script_eval_expression(&c[0]);
    script_eval_expression(&c[1]);
    script_eval_expression(&tail_short);
    script_eval_expression(&c[2]);
    script_eval_expression(&c[3]);
    script_eval_expression(&c[4]);
    script_eval_expression(&b0);
    script_eval_expression(&b1);
    script_eval_expression(&tail_word);
    FUN_0021f6e8((float)c[0] / fGpffff8d10, (float)c[1] / fGpffff8d10,
                 (float)c[2] / fGpffff8d10, (float)c[3] / fGpffff8d10,
                 (float)c[4] / fGpffff8d10, pair, tail_short, b0, b1,
                 tail_word);
    return 0;
}
