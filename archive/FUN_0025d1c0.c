
void FUN_0025d1c0(long param_1, ushort param_2, int param_3)

{
  ushort *puVar1;

  if (param_1 == 0)
  {
    DAT_00571dc0 = 0x1fe0;
    puVar1 = &DAT_00571dc0;
  }
  else
  {
    DAT_00571dd0 = 0;
    puVar1 = &DAT_00571dd0;
  }
  puVar1[5] = 0xa0;
  puVar1[1] = param_2;
  *(int *)(puVar1 + 2) = param_3;
  FUN_0025d0e0(param_3 + ((int)((uint)*puVar1 << 0x10) >> 0x15) * 0x1000000, 1);
  puVar1[4] = 0;
  return;
}
