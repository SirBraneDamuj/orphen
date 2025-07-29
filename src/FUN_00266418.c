
uint FUN_00266418(uint param_1)

{
  uint uVar1;
  uint uVar2;

  uVar2 = 1 << (param_1 & 7);
  if (0x8ff < (int)param_1 >> 3)
  {
    return 0;
  }
  uVar1 = (byte)(&DAT_00342b70)[(int)param_1 >> 3] ^ uVar2;
  (&DAT_00342b70)[(int)param_1 >> 3] = (byte)uVar1;
  return uVar1 & 0xff & uVar2;
}
