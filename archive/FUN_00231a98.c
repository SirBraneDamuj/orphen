
void FUN_00231a98(void)

{
  int iVar1;
  undefined8 uVar2;
  long lVar3;
  uint uVar4;
  undefined **ppuVar5;
  int *piVar6;
  int *piVar7;
  long lVar8;
  int iVar9;

  iVar9 = 0x70;
  lVar8 = 0;
  ppuVar5 = &PTR_FUN_0031c3c0;
  uVar4 = 0;
  uGpffffbcc0 = 0xffff;
  DAT_0031c458 = 0;
  do
  {
    if (*ppuVar5 == (undefined *)0x0)
    {
      uGpffffbcc0 = uGpffffbcc0 & ~(ushort)(1 << (uVar4 & 0x1f));
    }
    else if ((uVar4 == 6) && (iVar1 = FUN_002298d0(DAT_0058beb0), iVar1 - 1U < 2))
    {
      uGpffffbcc0 = uGpffffbcc0 & 0xffbf;
    }
    uVar4 = uVar4 + 1;
    ppuVar5 = ppuVar5 + 1;
  } while ((int)uVar4 < 7);
  piVar7 = &DAT_0031c464;
  uVar4 = 0;
  piVar6 = &DAT_0031c45c;
  do
  {
    uVar2 = FUN_0025b9e8(uVar4 + 0x3f);
    lVar3 = FUN_00238e68(uVar2, 0x14);
    if (lVar8 < lVar3)
    {
      lVar8 = lVar3;
    }
    if (((int)(uint)uGpffffbcc0 >> (uVar4 & 0x1f) & 1U) == 0)
    {
      piVar6[1] = 0x20404040;
    }
    else
    {
      piVar6[1] = 0x20808080;
    }
    piVar7[1] = iVar9;
    uVar4 = uVar4 + 1;
    iVar9 = iVar9 + -0x1e;
    iVar1 = *piVar6;
    piVar6 = piVar6 + 6;
    *piVar7 = (iVar1 * -0x14) / 2;
    piVar7 = piVar7 + 6;
  } while ((int)uVar4 < 7);
  DAT_0031c45c = (int)lVar8 + 0x20;
  uGpffffbcbc = 0;
  FUN_00237ad8();
  return;
}
