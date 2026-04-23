/*
 * Opcodes 0x110 / 0x111 — submit_single_scaled_coord_2words (shared handler)
 *
 * Original: FUN_00262cf0
 *
 * Reads two raw words and one coord. For 0x110 forwards to FUN_00212db0
 * with the coord scaled by fGpffff8d18; otherwise (0x111) forwards to
 * FUN_00212d60 with scale fGpffff8d1c.
 */

extern short sGpffffbd68;
extern float fGpffff8d18;
extern float fGpffff8d1c;
extern void script_eval_expression(void *out);
extern void FUN_00212db0(float, unsigned int, unsigned int);
extern void FUN_00212d60(float, unsigned int, unsigned int);

unsigned int op_0x110_0x111_submit_single_scaled_coord_2words(void)
{
  short opcode = sGpffffbd68;
  unsigned int w0, w1;
  int coord;
  script_eval_expression(&w0);
  script_eval_expression(&w1);
  script_eval_expression(&coord);
  if (opcode == 0x110)
    FUN_00212db0((float)coord / fGpffff8d18, w0, w1);
  else
    FUN_00212d60((float)coord / fGpffff8d1c, w0, w1);
  return 0;
}
