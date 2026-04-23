/*
 * Opcodes 0x77–0x7C — register read-modify-write
 * Shared handler: FUN_00260360, branches on DAT_00355cd8 (current opcode).
 *
 * Arguments: selector, var_id, value.
 *   FUN_0025d6c0(selector, DAT_00355044) selects the operating entity.
 *
 *   0x77 set : write = value
 *   0x78 and : write = read(var) & value
 *   0x79 or  : write = read(var) | value
 *   0x7A xor : write = read(var) ^ value
 *   0x7B add : write = read(var) + value
 *   0x7C sub : write = read(var) - value
 *
 * All paths return the new value via FUN_0025c8f8(var, new). FUN_0025c548
 * is the reader (same helper as opcode 0x76).
 */

extern void FUN_0025c258(void *);
extern void FUN_0025d6c0(unsigned int, void *);
extern unsigned int FUN_0025c548(unsigned int);
extern unsigned int FUN_0025c8f8(unsigned int, unsigned int);
extern short DAT_00355cd8;
extern void *DAT_00355044;

unsigned int op_0x77_through_0x7C_modify_register(void)
{
  unsigned int args[3];
  short sub = DAT_00355cd8;
  void *prev = DAT_00355044;
  FUN_0025c258(&args[0]);
  FUN_0025c258(&args[1]);
  FUN_0025c258(&args[2]);
  FUN_0025d6c0(args[0], prev);
  unsigned int cur, v = args[2];
  switch (sub)
  {
  case 0x77:
    return FUN_0025c8f8(args[1], v);
  case 0x78:
    cur = FUN_0025c548(args[1]);
    return FUN_0025c8f8(args[1], cur & v);
  case 0x79:
    cur = FUN_0025c548(args[1]);
    return FUN_0025c8f8(args[1], cur | v);
  case 0x7A:
    cur = FUN_0025c548(args[1]);
    return FUN_0025c8f8(args[1], cur ^ v);
  case 0x7B:
    cur = FUN_0025c548(args[1]);
    return FUN_0025c8f8(args[1], cur + v);
  case 0x7C:
    cur = FUN_0025c548(args[1]);
    return FUN_0025c8f8(args[1], cur - v);
  }
  return 0;
}
