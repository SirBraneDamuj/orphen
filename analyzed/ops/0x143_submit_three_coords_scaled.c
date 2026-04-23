/*
 * Opcode 0x143 — submit_three_coords_scaled
 *
 * Original: FUN_00265478
 *
 * Reads 3 coords, normalizes by DAT_00352cd8, forwards to FUN_002186e0.
 */

extern float DAT_00352cd8;
extern void  script_eval_expression(void *out);
extern void  FUN_002186e0(float, float, float);

unsigned int op_0x143_submit_three_coords_scaled(void) {
    int x, y, z;
    script_eval_expression(&x);
    script_eval_expression(&y);
    script_eval_expression(&z);
    FUN_002186e0((float)x / DAT_00352cd8,
                 (float)y / DAT_00352cd8,
                 (float)z / DAT_00352cd8);
    return 0;
}
