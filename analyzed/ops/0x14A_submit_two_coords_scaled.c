/*
 * Opcode 0x14A — submit_two_coords_scaled
 *
 * Original: FUN_002657b8
 *
 * Reads two coords, normalizes by DAT_00352ce0, forwards to FUN_0023ae60.
 */

extern float DAT_00352ce0;
extern void  script_eval_expression(void *out);
extern void  FUN_0023ae60(float, float);

void op_0x14A_submit_two_coords_scaled(void) {
    int a, b;
    script_eval_expression(&a);
    script_eval_expression(&b);
    FUN_0023ae60((float)a / DAT_00352ce0, (float)b / DAT_00352ce0);
}
