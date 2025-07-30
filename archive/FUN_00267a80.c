
/* WARNING: Removing unreachable block (ram,0x00267b58) */
/* WARNING: Removing unreachable block (ram,0x00267af8) */
/* WARNING: Removing unreachable block (ram,0x00267b88) */

void FUN_00267a80(float param_1, float param_2, float param_3, long param_4, long param_5)

{
  int iVar1;
  int iVar2;
  int iVar3;
  int iVar4;
  float fVar5;
  undefined4 uVar6;
  float fVar7;

  param_1 = param_1 - DAT_0058c0a8;
  param_2 = param_2 - DAT_0058c0ac;
  if (param_5 < 0)
  {
    param_5 = 100;
    fVar5 = fGpffff8d9c;
  }
  else
  {
    fVar5 = SQRT(param_1 * param_1 + param_2 * param_2 +
                 (param_3 - DAT_0058c0b0) * (param_3 - DAT_0058c0b0));
  }
  if (fVar5 < 14.0)
  {
    iVar1 = FUN_0030bd20(((14.0 - fVar5) * 128.0) / 14.0);
    iVar1 = (iVar1 * (int)param_5) / 100;
    fVar5 = SQRT(param_1 * param_1 + param_2 * param_2);
    iVar2 = FUN_0030bd20(((3.0 - fVar5) * 100.0) / 3.0);
    if (iVar2 < 0)
    {
      iVar2 = 0;
    }
    else if (iVar1 < iVar2)
    {
      iVar2 = iVar1;
    }
    if (fGpffff8da0 < fVar5)
    {
      uVar6 = FUN_00305408(param_2, param_1);
      fVar5 = (float)FUN_002166e8(uGpffffb6d4, uVar6);
      fVar7 = (float)FUN_00305130(fVar5 + fVar5);
      iVar3 = FUN_0030bd20((fVar7 - 1.0) * 40.0);
      if (0.0 < fVar5)
      {
        iVar3 = -iVar3;
      }
    }
    else
    {
      iVar3 = 0;
    }
    iVar4 = (iVar3 + 0x6e) * iVar1;
    iVar1 = (0x6e - iVar3) * iVar1;
    iVar3 = iVar4 + 0x7f;
    if (-1 < iVar4)
    {
      iVar3 = iVar4;
    }
    iVar3 = iVar3 >> 7;
    iVar4 = iVar1 + 0x7f;
    if (-1 < iVar1)
    {
      iVar4 = iVar1;
    }
    iVar4 = iVar4 >> 7;
    iVar1 = iVar2;
    if ((iVar2 <= iVar3) && (iVar1 = iVar3, 0x7f < iVar3))
    {
      iVar1 = 0x7f;
    }
    if ((iVar2 <= iVar4) && (iVar2 = iVar4, 0x7f < iVar4))
    {
      iVar2 = 0x7f;
    }
    if (cGpffffb664 != '\0')
    {
      if (param_4 < 0)
      {
        FUN_00206128(-(int)param_4);
        return;
      }
      FUN_002057c8(param_4, iVar1, iVar2);
      return;
    }
  }
  return;
}
