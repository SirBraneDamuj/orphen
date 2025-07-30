
void FUN_00207de8(int param_1)

{
  byte bVar1;
  short sVar2;
  ushort uVar3;
  short sVar4;
  long *plVar5;
  undefined *puVar6;
  undefined2 uVar7;
  uint uVar8;
  ulong uVar9;
  long lVar10;
  long lVar11;
  uint *puVar12;
  char *pcVar13;
  uint *puVar14;
  uint *puVar15;
  uint *puVar16;
  undefined2 *puVar17;
  undefined *puVar18;
  float *pfVar19;
  int iVar20;
  float *pfVar21;
  float fVar22;
  undefined4 uVar23;
  float fVar24;
  float fVar25;

  puVar6 = puGpffffb7b4;
  plVar5 = DAT_70000000;
  if (DAT_70000008 - (int)DAT_70000004 < 0x400)
  {
    return;
  }
  DAT_70000000 = DAT_70000000 + 6;
  if ((long *)0x70003fff < DAT_70000000)
  {
    FUN_0026bf90(0);
  }
  puVar15 = DAT_70000004;
  if (((*(uint *)(puVar6 + 0xc) & 0x80) == 0) || (*(short *)(puVar6 + 4) < 3))
    goto LAB_002083e0;
  *(undefined4 *)((int)plVar5 + 0x24) = 0;
  if (param_1 == iGpffffaca8)
  {
    if (puVar15 == puGpffffacb0)
    {
      uVar3 = *puGpffffacac;
      *(uint *)(plVar5 + 4) = (uint)uVar3;
      if (uVar3 < 0xff00)
      {
        *(undefined4 *)((int)plVar5 + 0x24) = 1;
      }
      goto LAB_00207ec8;
    }
    sVar2 = *(short *)(puVar6 + 6);
  }
  else
  {
  LAB_00207ec8:
    sVar2 = *(short *)(puVar6 + 6);
  }
  *(uint **)(plVar5 + 3) = puVar15;
  if (*(int *)((int)plVar5 + 0x24) == 0)
  {
    puVar15 = puVar15 + 2;
  }
  plVar5[1] = 0xd;
  *plVar5 = 2;
  *(short *)(puVar6 + 6) = sVar2 + 1;
  if ((short)(sVar2 + 1) != 0)
  {
    *plVar5 = 3;
    plVar5[1] = 0x1d;
  }
  *(undefined4 *)((int)plVar5 + 0x1c) = 0;
  uVar8 = *(uint *)(puVar6 + 0xc);
  if ((uVar8 & 0x1c000) != 0)
  {
    plVar5[1] = plVar5[1] | 0x40;
    if ((uVar8 & 0x4000) == 0)
    {
      uVar23 = 2;
      if (((uVar8 & 0x8000) == 0) && (uVar23 = 3, (uVar8 & 0x10000) == 0))
        goto LAB_00207f64;
    }
    else
    {
      uVar23 = 1;
    }
    *(undefined4 *)((int)plVar5 + 0x1c) = uVar23;
  }
LAB_00207f64:
  *(byte *)(plVar5 + 5) = (byte)((int)*(undefined4 *)(puVar6 + 0xc) >> 0x1d) & 1;
  *puVar15 = 0x6e03c000;
  *(undefined *)(puVar15 + 1) = puVar6[4];
  pcVar13 = (char *)((int)puVar15 + 5);
  if (*(short *)(puVar6 + 6) == 0)
  {
    *pcVar13 = '\0';
    *(undefined1 *)((int)puVar15 + 6) = 0;
  }
  else
  {
    bVar1 = puVar6[6];
    if (bVar1 < 0x18)
    {
      *pcVar13 = '\0';
    }
    else
    {
      *pcVar13 = (char)((ushort) * (short *)(puVar6 + 6) >> 8) + '\x01';
    }
    *(byte *)((int)puVar15 + 6) = bVar1;
  }
  *(undefined1 *)((int)puVar15 + 7) = 0;
  *(char *)(puVar15 + 2) =
      (char)(*(int *)((int)plVar5 + 0x1c) << 1) + (char)*(int *)((int)plVar5 + 0x1c);
  if ((char)plVar5[5] == '\0')
  {
    *(undefined1 *)((int)puVar15 + 9) = 0;
  }
  else
  {
    *(undefined1 *)((int)puVar15 + 9) = 1;
  }
  *(undefined2 *)((int)puVar15 + 10) = 0;
  puVar15[3] = 0;
  puVar15[4] = 0x65018003;
  lVar11 = *plVar5;
  sVar2 = *(short *)(puVar6 + 6);
  lVar10 = plVar5[1];
  puVar15[5] = (int)sVar2;
  puVar15[6] = 0x68018004;
  uVar9 = (long)*(short *)(puVar6 + 4) | lVar11 << 0x3c | lVar10 << 0x2f | 0x400000008000U;
  plVar5[2] = uVar9;
  puVar15[7] = *(uint *)(plVar5 + 2);
  puVar15[8] = (uint)(uVar9 >> 0x20);
  if (sVar2 == 0)
  {
    uVar7 = 0x41;
  }
  else
  {
    uVar7 = 0x412;
  }
  *(undefined2 *)(puVar15 + 9) = uVar7;
  sVar2 = *(short *)(puVar6 + 4);
  uVar3 = *(ushort *)(puVar6 + 4);
  sVar4 = *(short *)(puVar6 + 6);
  puVar15[10] = (int)sVar2 << 0x10 | 0x6e00c024;
  puVar15 = puVar15 + 0xb;
  if (0 < sVar2)
  {
    iVar20 = (int)(short)uVar3;
    puVar12 = (uint *)(puVar6 + 0x10);
    do
    {
      uVar8 = *puVar12;
      if (sVar4 == 0)
      {
        uVar8 = uVar8 & 0xffffff | (uVar8 & 0xfe000000) >> 1;
      }
      else
      {
        uVar8 = (uVar8 & 0xfefefefe) >> 1;
      }
      *puVar15 = uVar8;
      puVar15 = puVar15 + 1;
      iVar20 = iVar20 + -1;
      puVar12 = puVar12 + 1;
    } while (iVar20 != 0);
  }
  if (sVar4 == 0)
  {
    lVar10 = (long)*(short *)(puVar6 + 4);
  }
  else
  {
    if ((char)plVar5[5] == '\0')
    {
      uVar8 = (uint)uVar3 << 0x10;
      lVar10 = 0;
      *puVar15 = uVar8 | 0x65008039;
      puVar12 = puVar15 + 1;
      if (0 < (int)uVar8)
      {
        pfVar19 = (float *)(puVar6 + 0xb4);
        puVar17 = (undefined2 *)((int)puVar15 + 6);
        do
        {
          lVar10 = (long)((int)lVar10 + 1);
          puVar12 = puVar12 + 1;
          uVar7 = FUN_0030bd20(pfVar19[-1] * 4096.0);
          fVar22 = *pfVar19;
          puVar17[-1] = uVar7;
          pfVar19 = pfVar19 + 2;
          uVar7 = FUN_0030bd20(fVar22 * 4096.0);
          *puVar17 = uVar7;
          puVar17 = puVar17 + 2;
        } while (lVar10 < *(short *)(puVar6 + 4));
      }
    }
    else
    {
      uVar8 = (uint)uVar3 << 0x10;
      lVar10 = 0;
      *puVar15 = uVar8 | 0x69008039;
      puVar12 = puVar15 + 1;
      if (0 < (int)uVar8)
      {
        fVar22 = 4096.0;
        pfVar19 = (float *)(puVar6 + 0xb4);
        pfVar21 = (float *)(puVar6 + 0x2c);
        puVar16 = puVar15 + 2;
        puVar14 = puVar15 + 1;
        puVar12 = puVar15;
        do
        {
          puVar12 = (uint *)((int)puVar12 + 6);
          lVar10 = (long)((int)lVar10 + 1);
          puVar15 = (uint *)((int)puVar14 + 6);
          fVar25 = *pfVar21 * fVar22;
          pfVar21 = pfVar21 + 4;
          uVar7 = FUN_0030bd20(pfVar19[-1] * fVar25);
          fVar24 = *pfVar19;
          *(undefined2 *)(puVar16 + -1) = uVar7;
          pfVar19 = pfVar19 + 2;
          uVar7 = FUN_0030bd20(fVar24 * fVar25);
          *(undefined2 *)puVar12 = uVar7;
          uVar7 = FUN_0030bd20(fVar25);
          *(undefined2 *)puVar16 = uVar7;
          puVar16 = (uint *)((int)puVar16 + 6);
          puVar14 = puVar15;
        } while (lVar10 < *(short *)(puVar6 + 4));
        lVar10 = (long)*(short *)(puVar6 + 4);
        goto LAB_00208258;
      }
    }
    puVar15 = puVar12;
    lVar10 = (long)*(short *)(puVar6 + 4);
  }
LAB_00208258:
  fVar22 = fGpffff8040;
  sVar2 = *(short *)(puVar6 + 4);
  *puVar15 = (int)lVar10 << 0x10 | 0x6c008006;
  puVar15 = puVar15 + 1;
  if (0 < lVar10)
  {
    iVar20 = (int)sVar2;
    puVar18 = puVar6;
    do
    {
      if ((*(uint *)(puVar6 + 0xc) & 0x10000000) == 0)
      {
        uVar8 = FUN_0030bd20(*(undefined4 *)(puVar18 + 0x20));
        uVar23 = *(undefined4 *)(puVar18 + 0x24);
        *puVar15 = uVar8;
        uVar8 = FUN_0030bd20(uVar23);
        fVar24 = *(float *)(puVar18 + 0x28);
        puVar15[1] = uVar8;
        uVar8 = FUN_0030bd20(fVar24 * fVar22);
      }
      else
      {
        *puVar15 = *(uint *)(puVar18 + 0x20);
        puVar15[1] = *(uint *)(puVar18 + 0x24);
        uVar8 = *(uint *)(puVar18 + 0x28);
      }
      puVar15[2] = uVar8;
      puVar15[3] = 0xff;
      puVar18 = puVar18 + 0x10;
      iVar20 = iVar20 + -1;
      puVar15 = puVar15 + 4;
    } while (iVar20 != 0);
  }
  *puVar15 = 0x14000084;
  while (puVar15 = puVar15 + 1, ((uint)puVar15 & 0xf) != 0)
  {
    *puVar15 = 0;
  }
  if (*(int *)((int)plVar5 + 0x24) == 0)
  {
    iVar20 = param_1 * 0x10 + DAT_7000000c;
    **(uint **)(plVar5 + 3) =
        ((uint)((int)puVar15 - (int)*(uint **)(plVar5 + 3)) >> 4) - 1 | 0x20000000;
    *(undefined4 *)((int)plVar5[3] + 4) = *(undefined4 *)(iVar20 + 4);
    SYNC(0);
    *(int *)(iVar20 + 4) = (int)plVar5[3];
    puGpffffacac = *(ushort **)(plVar5 + 3);
    iGpffffaca8 = param_1;
  }
  else
  {
    *(uint *)(plVar5 + 4) = (int)plVar5[4] + ((uint)((int)puVar15 - (int)plVar5[3]) >> 4);
    *puGpffffacac = *(ushort *)(plVar5 + 4);
  }
  puGpffffb7b4 = &DAT_70000010;
  DAT_70000004 = puVar15;
  puGpffffacb0 = puVar15;
LAB_002083e0:
  DAT_70000000 = DAT_70000000 + -6;
  return;
}
