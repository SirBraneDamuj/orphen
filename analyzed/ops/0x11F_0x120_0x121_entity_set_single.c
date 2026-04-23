/*
 * Opcodes 0x11F / 0x120 / 0x121 — entity_set_single (shared FUN_00264be8)
 *
 * Selects a target the same way as 0x116/8, then reads ONE param and
 * forwards to FUN_00225740(target, param). 0x11F selects via expr+pool;
 * 0x120 uses uGpffffb788; 0x121 uses DAT_00345a2c. Any other opcode
 * yields target=0 (defensive).
 */

extern short sGpffffbd68;
extern int iGpffffb0d4;
extern unsigned int uGpffffb788;
extern unsigned int DAT_00345a2c;
extern void script_eval_expression(void *out);
extern void script_select_entity(unsigned int idx, int ctx);
extern void FUN_00225740(unsigned int target, unsigned int p0);

unsigned int op_0x11F_0x120_0x121_entity_set_single(void)
{
  int ctx = iGpffffb0d4;
  unsigned int target = uGpffffb788;
  unsigned int idx, param;
  if (sGpffffbd68 != 0x120)
  {
    if (sGpffffbd68 < 0x121)
    {
      target = 0;
      if (sGpffffbd68 == 0x11F)
      {
        script_eval_expression(&idx);
        script_select_entity(idx, ctx);
        target = *(unsigned int *)(intptr_t)(iGpffffb0d4 + 0x164);
      }
    }
    else
    {
      target = (sGpffffbd68 == 0x121) ? DAT_00345a2c : 0;
    }
  }
  script_eval_expression(&param);
  FUN_00225740(target, param);
  return 0;
}
