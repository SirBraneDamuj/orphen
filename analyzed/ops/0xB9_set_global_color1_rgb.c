/*
 * Opcode 0xB9 — set_global_color1_rgb
 *
 * Original: FUN_00263d10
 *
 * Reads three expressions r/g/b and packs them into uGpffffb704 as
 * (r << 16) | (g << 8) | b. Sets the global tint/colour register #1 used
 * by later renderer ops.
 */

extern unsigned int uGpffffb704;
extern void script_eval_expression(void *out); /* FUN_0025c258 */

int op_0xB9_set_global_color1_rgb(void)
{
  int r, g, b;
  script_eval_expression(&r);
  script_eval_expression(&g);
  script_eval_expression(&b);
  uGpffffb704 = ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)b;
  return 0;
}
