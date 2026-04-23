/*
 * Opcodes 0x3D, 0x3E, 0x3F, 0x40 — flag bucket query / clear / set / toggle
 * Handler: FUN_0025e560 (shared, branches on previous opcode byte)
 *
 * Reads one expression (flag id). Always queries the bit via FUN_00266368 and
 * returns that pre-modification state. Then, depending on the raw opcode byte
 * at DAT_00355cd0[-1]:
 *   '>' (0x3E)  -> FUN_002663a0(flag)  clear flag
 *   '?' (0x3F)  -> FUN_002663d8(flag)  set flag
 *   '@' (0x40)  -> FUN_00266418(flag)  toggle flag
 *   '=' (0x3D)  -> query only (no write)
 *
 * In debug builds (DAT_003555d3 != 0) a write with id < 800 triggers a warning
 * via FUN_0026bfc0(0x34ce70): "low flag ids are reserved for engine use".
 */

extern int FUN_00266368(int);  /* get flag bit */
extern void FUN_002663a0(int); /* clear flag */
extern void FUN_002663d8(int); /* set flag */
extern void FUN_00266418(int); /* toggle flag */
extern void FUN_0025c258(void *);
extern void FUN_0026bfc0(int, ...);
extern unsigned char *DAT_00355cd0;
extern char DAT_003555d3;

int op_0x3D_through_0x40_modify_flag_state(void)
{
  int id[4];
  char prev = (char)DAT_00355cd0[-1];
  FUN_0025c258(id);
  int prev_state = FUN_00266368(id[0]) != 0;
  if (DAT_003555d3 && (prev == '>' || prev == '@' || prev == '?') && id[0] < 800)
  {
    FUN_0026bfc0(0x34ce70);
  }
  if (prev == '>')
    FUN_002663a0(id[0]);
  else if (prev == '?')
    FUN_002663d8(id[0]);
  else if (prev == '@')
    FUN_00266418(id[0]);
  return prev_state;
}
