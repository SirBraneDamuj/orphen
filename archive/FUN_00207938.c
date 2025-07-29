
void FUN_00207938(ulong param_1, long param_2, int param_3, int param_4, short param_5, short param_6,
                  int param_7, int param_8, int param_9, int param_10, int param_11, undefined4 param_12)

{
  undefined4 *puVar1;
  long *plVar2;
  undefined2 uVar3;
  undefined4 uVar4;
  ulong uVar5;
  long lVar6;
  long *plVar7;
  long lVar8;
  short sVar9;
  short sVar10;
  short sVar11;
  short sVar12;
  char *pcVar13;
  undefined4 *puVar14;
  int iVar15;
  short sVar16;
  short sVar17;
  uint uVar18;

  plVar2 = DAT_70000000;
  if (DAT_70000008 - (int)DAT_70000004 < 0x400)
  {
    return;
  }
  DAT_70000000 = DAT_70000000 + 0xc;
  if ((long *)0x70003fff < DAT_70000000)
  {
    FUN_0026bf90(0);
  }
  if (param_2 < 0)
  {
    *(int *)(plVar2 + 10) = -(int)param_2;
    param_2 = 0xffff;
  }
  else
  {
    iVar15 = (int)param_2 >> 4;
    *(int *)(plVar2 + 10) = iVar15;
    if (iVar15 < 2)
    {
      uVar4 = 2;
    }
    else
    {
      if (iVar15 < 0x1000)
        goto LAB_00207a18;
      uVar4 = 0xfff;
    }
    *(undefined4 *)(plVar2 + 10) = uVar4;
  }
LAB_00207a18:
  sVar9 = (short)(param_3 << 4) + -0x8000;
  sVar17 = (short)(param_8 << 4);
  *(short *)((int)plVar2 + 0x1c) = sVar17;
  sVar10 = ((short)param_3 + param_5) * 0x10 + -0x8000;
  *(short *)(plVar2 + 3) = sVar17;
  sVar16 = (short)(param_9 << 4);
  *(short *)((int)plVar2 + 0x26) = sVar16;
  sVar17 = sVar17 + (short)(param_10 << 4);
  *(short *)((int)plVar2 + 0x1a) = sVar16;
  sVar11 = (short)(param_4 << 3) + -0x8000;
  sVar16 = sVar16 + (short)(param_11 << 4);
  sVar12 = ((short)param_4 + param_6) * 8 + -0x8000;
  *(short *)(plVar2 + 5) = sVar9;
  uVar18 = param_7 + 1;
  *(short *)(plVar2 + 7) = sVar10;
  *(short *)((int)plVar2 + 0x2a) = sVar11;
  *(short *)((int)plVar2 + 0x32) = sVar12;
  *(short *)(plVar2 + 4) = sVar17;
  *(short *)((int)plVar2 + 0x1e) = sVar16;
  iVar15 = 3;
  *(short *)(plVar2 + 6) = sVar9;
  *(short *)(plVar2 + 8) = sVar10;
  *(short *)((int)plVar2 + 0x42) = sVar11;
  *(short *)((int)plVar2 + 0x3a) = sVar12;
  *(short *)((int)plVar2 + 0x24) = sVar17;
  *(short *)((int)plVar2 + 0x22) = sVar16;
  plVar7 = plVar2;
  do
  {
    *(short *)((int)plVar7 + 0x2c) = (short)param_2;
    iVar15 = iVar15 + -1;
    *(undefined2 *)((int)plVar7 + 0x2e) = 0xff;
    plVar7 = plVar7 + 1;
  } while (-1 < iVar15);
  *(undefined4 **)(plVar2 + 9) = DAT_70000004;
  plVar2[1] = 0xd;
  *plVar2 = 2;
  if (uVar18 != 0)
  {
    *plVar2 = 3;
    plVar2[1] = 0x1d;
  }
  *(undefined4 *)((int)plVar2 + 0x4c) = 0;
  if ((param_1 & 1) == 0)
  {
    if ((param_1 & 2) == 0)
    {
      uVar4 = 3;
      if ((param_1 & 4) == 0)
        goto LAB_00207b54;
      uVar5 = plVar2[1];
    }
    else
    {
      uVar5 = plVar2[1];
      uVar4 = 2;
    }
  }
  else
  {
    uVar5 = plVar2[1];
    uVar4 = 1;
  }
  *(undefined4 *)((int)plVar2 + 0x4c) = uVar4;
  plVar2[1] = uVar5 | 0x40;
LAB_00207b54:
  *(undefined4 *)((int)DAT_70000004 + 8) = 0x6e03c000;
  *(undefined1 *)((int)DAT_70000004 + 0xc) = 4;
  pcVar13 = (char *)((int)DAT_70000004 + 0xd);
  if (uVar18 == 0)
  {
    *pcVar13 = '\0';
    *(undefined1 *)((int)DAT_70000004 + 0xe) = 0;
  }
  else
  {
    if ((uVar18 & 0xff) < 0x18)
    {
      *pcVar13 = '\0';
    }
    else
    {
      *pcVar13 = (char)(uVar18 >> 8) + '\x01';
    }
    *(char *)((int)DAT_70000004 + 0xe) = (char)uVar18;
  }
  *(undefined1 *)((int)DAT_70000004 + 0xf) = 0;
  *(char *)((int)DAT_70000004 + 0x10) =
      (char)(*(int *)((int)plVar2 + 0x4c) << 1) + (char)*(int *)((int)plVar2 + 0x4c);
  *(undefined1 *)((int)DAT_70000004 + 0x11) = 0;
  *(undefined2 *)((int)DAT_70000004 + 0x12) = 0;
  lVar8 = *plVar2;
  *(undefined4 *)((int)DAT_70000004 + 0x14) = 0;
  lVar6 = plVar2[1];
  *(undefined4 *)((int)DAT_70000004 + 0x18) = 0x65018003;
  *(uint *)((int)DAT_70000004 + 0x1c) = uVar18;
  uVar5 = lVar6 << 0x2f | lVar8 << 0x3c | 0x400000008004U;
  *(undefined4 *)((int)DAT_70000004 + 0x20) = 0x68018004;
  plVar2[2] = uVar5;
  *(int *)((int)DAT_70000004 + 0x24) = (int)plVar2[2];
  *(int *)((int)DAT_70000004 + 0x28) = (int)(uVar5 >> 0x20);
  if (uVar18 == 0)
  {
    uVar3 = 0x41;
  }
  else
  {
    uVar3 = 0x412;
  }
  *(undefined2 *)((int)DAT_70000004 + 0x2c) = uVar3;
  *(undefined4 *)((int)DAT_70000004 + 0x30) = 0x6e01c024;
  *(undefined4 *)((int)DAT_70000004 + 0x34) = param_12;
  puVar14 = (undefined4 *)((int)DAT_70000004 + 0x38);
  if (uVar18 != 0)
  {
    plVar7 = plVar2 + 3;
    iVar15 = 3;
    *puVar14 = 0x65048039;
    puVar14 = (undefined4 *)((int)DAT_70000004 + 0x3c);
    do
    {
      lVar6 = *plVar7;
      iVar15 = iVar15 + -1;
      plVar7 = (long *)((int)plVar7 + 4);
      *puVar14 = (int)lVar6;
      puVar14 = puVar14 + 1;
    } while (-1 < iVar15);
  }
  iVar15 = 3;
  *puVar14 = 0x6d04c006;
  DAT_70000004 = puVar14 + 1;
  plVar7 = plVar2;
  do
  {
    DAT_70000004 = DAT_70000004 + 2;
    iVar15 = iVar15 + -1;
    puVar14[1] = (int)plVar7[5];
    puVar1 = (undefined4 *)((int)plVar7 + 0x2c);
    plVar7 = plVar7 + 1;
    puVar14[2] = *puVar1;
    puVar14 = puVar14 + 2;
  } while (-1 < iVar15);
  *DAT_70000004 = 0x14000084;
  while (iVar15 = DAT_7000000c, DAT_70000004 = DAT_70000004 + 1, ((uint)DAT_70000004 & 0xf) != 0)
  {
    *DAT_70000004 = 0;
  }
  **(uint **)(plVar2 + 9) =
      ((uint)((int)DAT_70000004 - (int)*(uint **)(plVar2 + 9)) >> 4) - 1 | 0x20000000;
  *(undefined4 *)((int)plVar2[9] + 4) = *(undefined4 *)((int)plVar2[10] * 0x10 + iVar15 + 4);
  SYNC(0);
  DAT_70000000 = DAT_70000000 + -0xc;
  *(int *)((int)plVar2[10] * 0x10 + iVar15 + 4) = (int)plVar2[9];
  return;
}
