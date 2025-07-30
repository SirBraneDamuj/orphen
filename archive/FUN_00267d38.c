
void FUN_00267d38(long param_1, long param_2)

{
  int iVar1;

  if (param_2 != 0)
  {
    iVar1 = (int)param_2;
    FUN_00267a80(*(undefined4 *)(iVar1 + 0x20), *(undefined4 *)(iVar1 + 0x24),
                 *(undefined4 *)(iVar1 + 0x28), param_1, 100);
    return;
  }
  if (cGpffffb664 != '\0')
  {
    if (param_1 < 0)
    {
      FUN_00206128(-(int)param_1, 0x7f, 0x7f);
      return;
    }
    FUN_002057c8(param_1, 0x7f, 0x7f);
    return;
  }
  return;
}
