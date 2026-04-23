/*
 * Opcode 0xBA — set_global_color2_rgb
 *
 * Original: FUN_00263d60
 *
 * Same pattern as 0xB9 but writes uGpffffb708 (the second global colour
 * register).
 */

extern unsigned int uGpffffb708;
extern void script_eval_expression(void *out);

int op_0xBA_set_global_color2_rgb(void)
{
  int r, g, b;
  script_eval_expression(&r);
  script_eval_expression(&g);
  script_eval_expression(&b);
  uGpffffb708 = ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)b;
  return 0;
}
