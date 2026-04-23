/*
 * Opcode 0x10C — submit_seven_coords_4params
 *
 * Original: FUN_00262898
 *
 * Like 0x10B but with one extra trailing param. Forwards to FUN_00219d60
 * with seven coords scaled by fGpffff8d08 plus trailing zero.
 */

extern float fGpffff8d08;
extern void script_eval_expression(void *out);
extern void FUN_00219d60(float, float, float, float, float, float, float,
                         unsigned short, unsigned int, unsigned int,
                         unsigned int, int);

unsigned int op_0x10C_submit_seven_coords_4params(void)
{
  int c[7];
  unsigned short pair;
  unsigned int p0, p1, p2;
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
  script_eval_expression(&p2);
  FUN_00219d60((float)c[0] / fGpffff8d08, (float)c[1] / fGpffff8d08,
               (float)c[2] / fGpffff8d08, (float)c[3] / fGpffff8d08,
               (float)c[4] / fGpffff8d08, (float)c[5] / fGpffff8d08,
               (float)c[6] / fGpffff8d08, pair, p0, p1, p2, 0);
  return 0;
}
