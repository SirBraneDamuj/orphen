
void FUN_00206128(int param_1, ulong param_2, ulong param_3)

{
  undefined2 uVar1;
  int iVar2;
  int iVar3;

  iVar3 = param_1 + 3;
  iVar2 = DAT_003555b4 - *(int *)(&DAT_00314bb0 + param_1 * 4);
  if (iVar2 < 0)
  {
    iVar2 = -iVar2;
  }
  if (3 < iVar2)
  {
    iVar2 = iVar3 * 0x2c;
    *(int *)(&DAT_00314bb0 + param_1 * 4) = DAT_003555b4;
    if ((byte)(&DAT_003567b6)[iVar2] == param_2)
    {
      if ((byte)(&DAT_003567b7)[iVar2] == param_3)
      {
        return;
      }
      uVar1 = *(undefined2 *)(&DAT_003567b4 + iVar2);
    }
    else
    {
      uVar1 = *(undefined2 *)(&DAT_003567b4 + iVar2);
    }
    (&DAT_003567b6)[iVar2] = (char)param_2;
    (&DAT_003567b7)[iVar2] = (char)param_3;
    FUN_00206048(iVar3, uVar1);
    if ('\x01' < (char)(&DAT_003567b0)[iVar2])
    {
      FUN_00204d10(0x4043, (&DAT_003567d8)[iVar3 * 0xb], *(undefined2 *)(&DAT_003567b8 + iVar2),
                   *(undefined2 *)(&DAT_003567ba + iVar2));
    }
  }
  return;
}
