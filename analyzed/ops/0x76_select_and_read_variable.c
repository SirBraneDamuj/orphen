/*
 * Opcode 0x76 FUN_00260318 — select entity then read variable/register
 *
 * Two expressions (entity index, variable id). Selects entity via
 * FUN_0025d6c0 into DAT_00355044, then returns FUN_0025c548(var) which is
 * the shared register-read helper (script variable / work memory indirect
 * via current selected entity context).
 */
extern void FUN_0025c258(void *);
extern void FUN_0025d6c0(unsigned int, void *);
extern unsigned int FUN_0025c548(unsigned int);
extern void *DAT_00355044;

unsigned int op_0x76_select_and_read(void)
{
  unsigned int args[2];
  void *prev = DAT_00355044;
  FUN_0025c258(&args[0]);
  FUN_0025c258(&args[1]);
  FUN_0025d6c0(args[0], prev);
  return FUN_0025c548(args[1]);
}
