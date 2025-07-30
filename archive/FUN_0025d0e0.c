
void FUN_0025d0e0(undefined4 param_1, char param_2)

{
  undefined4 uVar1;
  int iVar2;
  undefined1 uVar3;
  undefined1 uVar4;
  int iVar5;
  undefined4 *puVar6;
  undefined4 *puVar7;
  undefined4 uStack_20;

  iVar2 = DAT_00355724;
  puVar7 = (undefined4 *)(DAT_00355724 + 0x28);
  puVar6 = (undefined4 *)(DAT_00355724 + 0x10);
  iVar5 = 3;
  *(undefined4 *)(DAT_00355724 + 0x34) = 0xc3600000;
  *(undefined4 *)(iVar2 + 0x44) = 0xc3600000;
  *(undefined4 *)(iVar2 + 0x20) = 0xc3a00000;
  *(undefined4 *)(iVar2 + 0x40) = 0x43a00000;
  *(undefined4 *)(iVar2 + 0x24) = 0x43600000;
  *(undefined4 *)(iVar2 + 0x30) = 0xc3a00000;
  *(undefined4 *)(iVar2 + 0x50) = 0x43a00000;
  *(undefined4 *)(iVar2 + 0x54) = 0x43600000;
  uVar1 = DAT_00352b68;
  uStack_20._0_1_ = (undefined1)param_1;
  uVar3 = (undefined1)uStack_20;
  uStack_20._2_1_ = (undefined1)((uint)param_1 >> 0x10);
  uVar4 = uStack_20._2_1_;
  uStack_20._3_1_ = (undefined1)((uint)param_1 >> 0x18);
  uStack_20._0_3_ = CONCAT12(uVar3, (short)param_1);
  uStack_20 = CONCAT31(uStack_20._1_3_, uVar4);
  do
  {
    iVar5 = iVar5 + -1;
    *puVar7 = uVar1;
    *puVar6 = uStack_20;
    puVar7 = puVar7 + 4;
    puVar6 = puVar6 + 1;
  } while (-1 < iVar5);
  *(undefined2 *)(iVar2 + 4) = 4;
  *(undefined2 *)(iVar2 + 6) = 0xffff;
  *(undefined4 *)(iVar2 + 0xc) = 0x40180;
  if (param_2 != '\0')
  {
    *(undefined4 *)(iVar2 + 0xc) = 0x44180;
  }
  FUN_00207de8(0x1007);
  return;
}
