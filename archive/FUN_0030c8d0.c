
/* WARNING: Type propagation algorithm not settling */

int FUN_0030c8d0(undefined4 param_1, undefined8 param_2, undefined8 param_3, ulong *param_4)

{
  bool bVar1;
  uint uVar2;
  char cVar3;
  int *piVar4;
  int iVar5;
  char *pcVar6;
  long lVar7;
  long lVar8;
  ushort uVar9;
  char *pcVar10;
  undefined8 uVar11;
  undefined8 uVar12;
  undefined8 uVar13;
  ulong uVar14;
  int *piVar15;
  int iVar16;
  int *piVar17;
  char *pcVar18;
  ulong *puVar19;
  ulong *puVar20;
  uint uVar21;
  char *pcVar22;
  undefined1 auStack_2f0[16];
  int *piStack_2e0;
  int iStack_2dc;
  int iStack_2d8;
  int aiStack_2d0[16];
  char acStack_290[348];
  char acStack_134[4];
  char cStack_130;
  undefined1 uStack_12f;
  char cStack_120;
  char acStack_11f[3];
  int iStack_11c;
  undefined4 uStack_118;
  char *pcStack_114;
  int iStack_110;
  undefined4 uStack_10c;
  char *pcStack_108;
  uint uStack_104;
  int iStack_100;
  char *pcStack_fc;
  int iStack_f8;
  ulong uStack_f0;
  int iStack_e8;
  char *pcStack_e4;
  char *pcStack_e0;
  char *pcStack_dc;
  int *piStack_d8;
  undefined4 *puStack_d4;
  int iStack_d0;
  undefined4 uStack_cc;
  int iStack_c0;
  undefined4 uStack_bc;
  undefined4 uStack_b0;
  undefined4 uStack_ac;

  uStack_10c = param_1;
  piVar4 = (int *)FUN_00310318();
  iStack_f8 = *piVar4;
  uStack_118 = 0;
  iVar5 = (int)param_2;
  if (*(int *)(iVar5 + 0x54) == 0)
  {
    *(undefined **)(iVar5 + 0x54) = PTR_DAT_0034b01c;
    iVar16 = *(int *)(iVar5 + 0x54);
  }
  else
  {
    iVar16 = *(int *)(iVar5 + 0x54);
  }
  if (*(int *)(iVar16 + 0x38) == 0)
  {
    FUN_0030f8d0();
    uVar9 = *(ushort *)(iVar5 + 0xc);
  }
  else
  {
    uVar9 = *(ushort *)(iVar5 + 0xc);
  }
  if (((uVar9 & 8) == 0) || (*(int *)(iVar5 + 0x10) == 0))
  {
    lVar7 = FUN_0030e150(param_2);
    if (lVar7 != 0)
    {
      return -1;
    }
    uVar9 = *(ushort *)(iVar5 + 0xc);
  }
  else
  {
    uVar9 = *(ushort *)(iVar5 + 0xc);
  }
  piStack_2e0 = aiStack_2d0;
  if (((uVar9 & 0x1a) == 10) && (-1 < *(short *)(iVar5 + 0xe)))
  {
    iVar5 = FUN_0030c7e8(param_2, param_3, param_4);
    return iVar5;
  }
  piStack_d8 = &iStack_11c;
  puStack_d4 = &uStack_118;
  iStack_2d8 = 0;
  iStack_2dc = 0;
  pcStack_108 = (char *)param_3;
  iStack_100 = 0;
  pcVar10 = pcStack_108;
  piVar4 = piStack_2e0;
LAB_0030c9d8:
  lVar7 = FUN_00310e60(PTR_DAT_0034b01c, piStack_d8, pcStack_108, DAT_0034b020, puStack_d4);
  if (0 < lVar7)
    goto code_r0x0030ca04;
  goto LAB_0030ca1c;
code_r0x0030ca04:
  pcStack_108 = pcStack_108 + (int)lVar7;
  if (iStack_11c != 0x25)
    goto LAB_0030c9d8;
  pcStack_108 = pcStack_108 + -1;
LAB_0030ca1c:
  iVar16 = (int)pcStack_108 - (int)pcVar10;
  if (iVar16 != 0)
  {
    piVar4[1] = iVar16;
    *piVar4 = (int)pcVar10;
    piVar4 = piVar4 + 2;
    iStack_2d8 = iStack_2d8 + iVar16;
    iStack_2dc = iStack_2dc + 1;
    if (7 < iStack_2dc)
    {
      lVar8 = FUN_0030c7a0(param_2, &piStack_2e0);
      piVar4 = aiStack_2d0;
      if (lVar8 != 0)
        goto LAB_0030de2c;
    }
    iStack_100 = iStack_100 + iVar16;
  }
  if (0 < lVar7)
  {
    acStack_11f[0] = '\0';
    pcStack_108 = pcStack_108 + 1;
    uStack_104 = 0;
    pcStack_e4 = (char *)0x0;
    pcStack_fc = (char *)0x0;
    puVar19 = param_4;
    pcVar10 = (char *)0xffffffff;
  LAB_0030caa0:
    lVar7 = (long)*pcStack_108;
    pcStack_108 = pcStack_108 + 1;
  LAB_0030caac:
    puVar20 = puVar19;
    switch ((int)lVar7)
    {
    case 0x20:
      goto switchD_0030cacc_caseD_20;
    default:
      if (lVar7 == 0)
        goto LAB_0030de0c;
      pcVar18 = acStack_290;
      acStack_290[0] = (char)lVar7;
      pcVar6 = (char *)0x1;
      acStack_11f[0] = '\0';
      param_4 = puVar19;
      break;
    case 0x23:
      uStack_104 = uStack_104 | 1;
      goto LAB_0030caa0;
    case 0x2a:
      puVar20 = puVar19 + 1;
      pcStack_fc = *(char **)puVar19;
      puVar19 = puVar20;
      if ((int)pcStack_fc < 0)
      {
        pcStack_fc = (char *)-(int)pcStack_fc;
        goto switchD_0030cacc_caseD_2d;
      }
      goto LAB_0030caa0;
    case 0x2b:
      acStack_11f[0] = '+';
      goto LAB_0030caa0;
    case 0x2d:
    switchD_0030cacc_caseD_2d:
      uStack_104 = uStack_104 | 4;
      puVar19 = puVar20;
      goto LAB_0030caa0;
    case 0x2e:
      cVar3 = *pcStack_108;
      lVar7 = (long)cVar3;
      pcStack_108 = pcStack_108 + 1;
      if (lVar7 == 0x2a)
      {
        puVar20 = puVar19 + 1;
        pcVar18 = *(char **)puVar19;
        puVar19 = puVar20;
        pcVar10 = (char *)0xffffffff;
        if (-2 < (int)pcVar18)
        {
          pcVar10 = pcVar18;
        }
        goto LAB_0030caa0;
      }
      pcVar18 = (char *)0x0;
      while ((int)cVar3 - 0x30U < 10)
      {
        pcVar18 = (char *)((int)pcVar18 * 10 + -0x30 + (int)lVar7);
        cVar3 = *pcStack_108;
        lVar7 = (long)cVar3;
        pcStack_108 = pcStack_108 + 1;
      }
      pcVar10 = (char *)0xffffffff;
      if (-2 < (int)pcVar18)
      {
        pcVar10 = pcVar18;
      }
      goto LAB_0030caac;
    case 0x30:
      uStack_104 = uStack_104 | 0x80;
      goto LAB_0030caa0;
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
    case 0x39:
      goto switchD_0030cacc_caseD_31;
    case 0x44:
      uStack_104 = uStack_104 | 0x10;
    case 100:
    case 0x69:
      if ((uStack_104 & 0x10) == 0)
      {
        if ((uStack_104 & 0x40) == 0)
        {
          uVar14 = (ulong)(int)(uint)*puVar19;
        }
        else
        {
          uVar14 = (ulong)(short)(ushort)*puVar19;
        }
      }
      else
      {
        uVar14 = *puVar19;
      }
      iVar16 = 1;
      if ((long)uVar14 < 0)
      {
        uVar14 = -uVar14;
        acStack_11f[0] = '-';
      }
    LAB_0030d0c8:
      param_4 = puVar19 + 1;
      if (-1 < (int)pcVar10)
      {
        uStack_104 = uStack_104 & 0xffffff7f;
      }
      uVar21 = uStack_104;
      pcVar18 = acStack_134;
      pcStack_e4 = pcVar10;
      if ((uVar14 == 0) && (pcVar10 == (char *)0x0))
        goto LAB_0030d21c;
      if (iVar16 == 1)
        goto joined_r0x0030d188;
      if (iVar16 == 0)
      {
        do
        {
          pcVar10 = pcVar18;
          pcVar18 = pcVar10 + -1;
          lVar8 = (uVar14 & 7) + 0x30;
          uVar14 = uVar14 >> 3;
          *pcVar18 = (char)lVar8;
        } while (uVar14 != 0);
        if (((uStack_104 & 1) != 0) && (lVar8 != 0x30))
        {
          pcVar18 = pcVar10 + -2;
          *pcVar18 = '0';
        }
        goto LAB_0030d21c;
      }
      if (iVar16 == 2)
        goto LAB_0030d1f0;
      pcVar18 = "bug in vfprintf: bad base";
      uVar21 = uStack_104 & 0x84;
      pcVar6 = (char *)FUN_0030c4a8(0x351bb8);
      goto LAB_0030d244;
    case 0x45:
    case 0x47:
    case 0x65:
    case 0x66:
    case 0x67:
      if (pcVar10 == (char *)0xffffffff)
      {
        pcVar10 = (char *)0x6;
      }
      else if (((lVar7 == 0x67) || (lVar7 == 0x47)) && (pcVar10 == (char *)0x0))
      {
        pcVar10 = (char *)0x1;
      }
      param_4 = puVar19 + 1;
      if ((uStack_104 & 8) == 0)
      {
        uStack_f0 = *puVar19;
      }
      else
      {
        uStack_f0 = *puVar19;
      }
      lVar8 = FUN_00312058(uStack_f0);
      if (lVar8 == 0)
      {
        lVar8 = FUN_003120a0(uStack_f0);
        if (lVar8 == 0)
        {
          uStack_104 = uStack_104 | 0x100;
          pcVar18 = (char *)FUN_0030de70(uStack_10c, uStack_f0, pcVar10, uStack_104, &cStack_120,
                                         &pcStack_114, lVar7, &iStack_110);
          if ((lVar7 == 0x67) || (lVar7 == 0x47))
          {
            if (((int)pcStack_114 < -3) || ((int)pcVar10 < (int)pcStack_114))
            {
              bVar1 = lVar7 != 0x67;
              lVar7 = 0x65;
              if (bVar1)
              {
                lVar7 = 0x45;
              }
            }
            else
            {
              lVar7 = 0x67;
            }
          }
          pcVar6 = pcStack_114 + -1;
          if (lVar7 < 0x66)
          {
            pcStack_114 = pcVar6;
            iStack_e8 = FUN_0030e018(auStack_2f0, pcVar6, lVar7);
            pcVar6 = (char *)(iStack_e8 + iStack_110);
            if ((1 < iStack_110) || ((uStack_104 & 1) != 0))
            {
              pcVar6 = pcVar6 + 1;
            }
          }
          else if (lVar7 == 0x66)
          {
            if ((int)pcStack_114 < 1)
            {
              pcVar6 = pcVar10 + 2;
            }
            else if ((pcVar10 != (char *)0x0) || (pcVar6 = pcStack_114, (uStack_104 & 1) != 0))
            {
              pcVar6 = pcStack_114 + 1 + (int)pcVar10;
            }
          }
          else if ((int)pcStack_114 < iStack_110)
          {
            pcVar6 = (char *)(iStack_110 + 1);
            if ((int)pcStack_114 < 1)
            {
              pcVar6 = (char *)((iStack_110 + 2) - (int)pcStack_114);
            }
          }
          else
          {
            pcVar6 = pcStack_114 + (uStack_104 & 1);
          }
          uVar21 = uStack_104 & 0x84;
          if (cStack_120 != '\0')
          {
            acStack_11f[0] = '-';
          }
        }
        else
        {
          pcVar6 = (char *)0x3;
          pcVar18 = "NaN";
          uVar21 = uStack_104 & 0x84;
        }
        goto LAB_0030d244;
      }
      lVar8 = FUN_0030b018(uStack_f0, 0);
      if (lVar8 < 0)
      {
        acStack_11f[0] = '-';
      }
      pcVar6 = (char *)0x3;
      pcVar18 = "Inf";
      break;
    case 0x4c:
      uStack_104 = uStack_104 | 8;
      goto LAB_0030caa0;
    case 0x4f:
      uStack_104 = uStack_104 | 0x10;
    case 0x6f:
      if ((uStack_104 & 0x10) == 0)
      {
        if ((uStack_104 & 0x40) == 0)
        {
          uVar14 = (ulong)(uint)*puVar19;
        }
        else
        {
          uVar14 = (ulong)(ushort)*puVar19;
        }
      }
      else
      {
        uVar14 = *puVar19;
      }
      iVar16 = 0;
    LAB_0030d0c4:
      acStack_11f[0] = '\0';
      goto LAB_0030d0c8;
    case 0x55:
      uStack_104 = uStack_104 | 0x10;
    case 0x75:
      if ((uStack_104 & 0x10) == 0)
      {
        if ((uStack_104 & 0x40) == 0)
        {
          uVar14 = (ulong)(uint)*puVar19;
        }
        else
        {
          uVar14 = (ulong)(ushort)*puVar19;
        }
      }
      else
      {
        uVar14 = *puVar19;
      }
      iVar16 = 1;
      goto LAB_0030d0c4;
    case 0x58:
      pcStack_dc = "0123456789ABCDEF";
      goto LAB_0030d070;
    case 99:
      param_4 = puVar19 + 1;
      acStack_290[0] = (char)*puVar19;
      pcVar18 = acStack_290;
      pcVar6 = (char *)0x1;
      acStack_11f[0] = '\0';
      uVar21 = uStack_104 & 0x84;
      goto LAB_0030d244;
    case 0x68:
      uStack_104 = uStack_104 | 0x40;
      goto LAB_0030caa0;
    case 0x6c:
      if (*pcStack_108 == 'l')
      {
        pcStack_108 = pcStack_108 + 1;
        uStack_104 = uStack_104 | 0x20;
      }
      else
      {
        uStack_104 = uStack_104 | 0x10;
      }
      goto LAB_0030caa0;
    case 0x6e:
      pcVar10 = pcStack_108;
      if ((uStack_104 & 0x10) == 0)
      {
        if ((uStack_104 & 0x40) == 0)
        {
          param_4 = puVar19 + 1;
          **(int **)puVar19 = iStack_100;
        }
        else
        {
          param_4 = puVar19 + 1;
          **(undefined2 **)puVar19 = (short)iStack_100;
        }
      }
      else
      {
        param_4 = puVar19 + 1;
        **(long **)puVar19 = (long)iStack_100;
      }
      goto LAB_0030c9d8;
    case 0x70:
      pcStack_dc = "0123456789abcdef";
      uStack_104 = uStack_104 | 2;
      lVar7 = 0x78;
      uVar14 = (ulong)(int)(uint)*puVar19;
      goto LAB_0030d0c0;
    case 0x71:
      uStack_104 = uStack_104 | 0x20;
      goto LAB_0030caa0;
    case 0x73:
      param_4 = puVar19 + 1;
      pcVar18 = *(char **)puVar19;
      if (pcVar18 == (char *)0x0)
      {
        pcVar18 = "(null)";
      }
      if ((int)pcVar10 < 0)
      {
        pcVar6 = (char *)FUN_0030c4a8(pcVar18);
      }
      else
      {
        lVar8 = FUN_00310e9c(pcVar18, 0, pcVar10);
        pcVar22 = (char *)((int)lVar8 - (int)pcVar18);
        pcVar6 = pcVar10;
        if ((lVar8 != 0) && (pcVar6 = pcVar22, (int)pcVar10 < (int)pcVar22))
        {
          pcVar6 = pcVar10;
        }
      }
      acStack_11f[0] = '\0';
      uVar21 = uStack_104 & 0x84;
      goto LAB_0030d244;
    case 0x78:
      pcStack_dc = "0123456789abcdef";
    LAB_0030d070:
      if ((uStack_104 & 0x10) == 0)
      {
        if ((uStack_104 & 0x40) == 0)
        {
          uVar14 = (ulong)(uint)*puVar19;
        }
        else
        {
          uVar14 = (ulong)(ushort)*puVar19;
        }
      }
      else
      {
        uVar14 = *puVar19;
      }
      iVar16 = 2;
      if ((uStack_104 & 1) != 0)
      {
        if (uVar14 != 0)
        {
          uStack_104 = uStack_104 | 2;
        }
      LAB_0030d0c0:
        iVar16 = 2;
      }
      goto LAB_0030d0c4;
    }
    uVar21 = uStack_104 & 0x84;
    goto LAB_0030d244;
  }
LAB_0030de0c:
  if ((iStack_2d8 != 0) && (lVar7 = FUN_0030c7a0(param_2, &piStack_2e0), lVar7 != 0))
  {
    uVar9 = *(ushort *)(iVar5 + 0xc);
    goto LAB_0030de30;
  }
  goto LAB_0030de2c;
joined_r0x0030d188:
  while (9 < uVar14)
  {
    cVar3 = FUN_0030a108(uVar14, 10);
    pcVar18 = pcVar18 + -1;
    *pcVar18 = cVar3 + '0';
    uVar14 = FUN_00309b48(uVar14, 10);
  }
  pcVar18 = pcVar18 + -1;
  *pcVar18 = (char)uVar14 + '0';
LAB_0030d21c:
  uVar21 = uVar21 & 0x84;
  pcVar6 = acStack_134 + -(int)pcVar18;
LAB_0030d244:
  piVar17 = piVar4 + 2;
  pcStack_e0 = pcStack_e4;
  if ((int)pcStack_e4 <= (int)pcVar6)
  {
    pcStack_e0 = pcVar6;
  }
  if (acStack_11f[0] == '\0')
  {
    pcStack_e0 = pcStack_e0 + (uStack_104 & 2);
  }
  else
  {
    pcStack_e0 = pcStack_e0 + 1;
  }
  if (uVar21 == 0)
  {
    iVar16 = (int)pcStack_fc - (int)pcStack_e0;
    if (0 < iVar16)
    {
      uVar13 = 0x350000;
      piVar15 = piVar4;
      if (0x10 < iVar16)
      {
        uVar12 = 0x351b50;
        uVar11 = 0x10;
        do
        {
          piVar15[1] = (int)uVar11;
          *piVar15 = (int)uVar12;
          iStack_2d8 = iStack_2d8 + 0x10;
          iStack_2dc = iStack_2dc + 1;
          piVar15 = piVar17;
          if (7 < iStack_2dc)
          {
            uStack_cc = (undefined4)((ulong)uVar11 >> 0x20);
            uStack_bc = (undefined4)((ulong)uVar12 >> 0x20);
            uStack_b0 = (undefined4)uVar13;
            uStack_ac = (undefined4)((ulong)uVar13 >> 0x20);
            iStack_d0 = (int)uVar11;
            iStack_c0 = (int)uVar12;
            lVar8 = FUN_0030c7a0(param_2, &piStack_2e0);
            uVar11 = CONCAT44(uStack_cc, iStack_d0);
            uVar12 = CONCAT44(uStack_bc, iStack_c0);
            uVar13 = CONCAT44(uStack_ac, uStack_b0);
            if (lVar8 != 0)
              goto LAB_0030de2c;
            piVar15 = aiStack_2d0;
          }
          iVar16 = iVar16 + -0x10;
          piVar17 = piVar15 + 2;
        } while (0x10 < iVar16);
      }
      piVar4 = piVar17;
      piVar15[1] = iVar16;
      *piVar15 = (int)uVar13 + 0x1b50;
      iStack_2dc = iStack_2dc + 1;
      iStack_2d8 = iStack_2d8 + iVar16;
      if (7 < iStack_2dc)
      {
        lVar8 = FUN_0030c7a0(param_2, &piStack_2e0);
        piVar4 = aiStack_2d0;
        if (lVar8 != 0)
          goto LAB_0030de2c;
      }
    }
  }
  if (acStack_11f[0] == '\0')
  {
    iVar16 = iStack_2dc;
    if ((uStack_104 & 2) != 0)
    {
      uStack_12f = (undefined1)lVar7;
      cStack_130 = '0';
      piVar4[1] = 2;
      *piVar4 = (int)&cStack_130;
      iStack_2d8 = iStack_2d8 + 2;
      goto joined_r0x0030d3e4;
    }
  }
  else
  {
    *piVar4 = (int)acStack_11f;
    piVar4[1] = 1;
    iStack_2d8 = iStack_2d8 + 1;
  joined_r0x0030d3e4:
    piVar4 = piVar4 + 2;
    iVar16 = iStack_2dc + 1;
    if (7 < iStack_2dc + 1)
    {
      iStack_2dc = iStack_2dc + 1;
      lVar8 = FUN_0030c7a0(param_2, &piStack_2e0);
      piVar4 = aiStack_2d0;
      iVar16 = iStack_2dc;
      if (lVar8 != 0)
        goto LAB_0030de2c;
    }
  }
  iStack_2dc = iVar16;
  if (uVar21 == 0x80)
  {
    iVar16 = (int)pcStack_fc - (int)pcStack_e0;
    if (0 < iVar16)
    {
      uVar13 = 0x350000;
      if (0x10 < iVar16)
      {
        piVar4[1] = 0x10;
        while (true)
        {
          *piVar4 = (int)"0000000000000000Inf";
          piVar4 = piVar4 + 2;
          iStack_2d8 = iStack_2d8 + 0x10;
          iStack_2dc = iStack_2dc + 1;
          if (7 < iStack_2dc)
          {
            iStack_d0 = (int)uVar13;
            uStack_cc = (undefined4)((ulong)uVar13 >> 0x20);
            lVar8 = FUN_0030c7a0(param_2, &piStack_2e0);
            uVar13 = CONCAT44(uStack_cc, iStack_d0);
            if (lVar8 != 0)
              goto LAB_0030de2c;
            piVar4 = aiStack_2d0;
          }
          iVar16 = iVar16 + -0x10;
          if (iVar16 < 0x11)
            break;
          piVar4[1] = 0x10;
        }
      }
      piVar4[1] = iVar16;
      *piVar4 = (int)uVar13 + 0x1b60;
      piVar4 = piVar4 + 2;
      iStack_2dc = iStack_2dc + 1;
      iStack_2d8 = iStack_2d8 + iVar16;
      if (7 < iStack_2dc)
      {
        lVar8 = FUN_0030c7a0(param_2, &piStack_2e0);
        piVar4 = aiStack_2d0;
        if (lVar8 != 0)
          goto LAB_0030de2c;
      }
    }
  }
  iVar16 = (int)pcStack_e4 - (int)pcVar6;
  if (0 < iVar16)
  {
    uVar13 = 0x350000;
    if (0x10 < iVar16)
    {
      piVar4[1] = 0x10;
      while (true)
      {
        *piVar4 = (int)"0000000000000000Inf";
        piVar4 = piVar4 + 2;
        iStack_2d8 = iStack_2d8 + 0x10;
        iStack_2dc = iStack_2dc + 1;
        if (7 < iStack_2dc)
        {
          iStack_d0 = (int)uVar13;
          uStack_cc = (undefined4)((ulong)uVar13 >> 0x20);
          lVar8 = FUN_0030c7a0(param_2, &piStack_2e0);
          uVar13 = CONCAT44(uStack_cc, iStack_d0);
          if (lVar8 != 0)
            goto LAB_0030de2c;
          piVar4 = aiStack_2d0;
        }
        iVar16 = iVar16 + -0x10;
        if (iVar16 < 0x11)
          break;
        piVar4[1] = 0x10;
      }
    }
    piVar4[1] = iVar16;
    *piVar4 = (int)uVar13 + 0x1b60;
    piVar4 = piVar4 + 2;
    iStack_2dc = iStack_2dc + 1;
    iStack_2d8 = iStack_2d8 + iVar16;
    if (7 < iStack_2dc)
    {
      lVar8 = FUN_0030c7a0(param_2, &piStack_2e0);
      piVar4 = aiStack_2d0;
      if (lVar8 != 0)
        goto LAB_0030de2c;
    }
  }
  if ((uStack_104 & 0x100) == 0)
  {
    piVar4[1] = (int)pcVar6;
    *piVar4 = (int)pcVar18;
    iStack_2d8 = iStack_2d8 + (int)pcVar6;
  LAB_0030dccc:
    piVar4 = piVar4 + 2;
    iStack_2dc = iStack_2dc + 1;
    bVar1 = iStack_2dc < 8;
  LAB_0030dcdc:
    if (bVar1)
      goto LAB_0030dcfc;
  }
  else
  {
    if (lVar7 < 0x66)
    {
      if ((iStack_110 < 2) && ((uStack_104 & 1) == 0))
      {
        *piVar4 = (int)pcVar18;
        piVar4[1] = 1;
        iStack_2d8 = iStack_2d8 + 1;
      joined_r0x0030dbb4:
        iStack_2dc = iStack_2dc + 1;
        piVar4 = piVar4 + 2;
        if (7 < iStack_2dc)
        {
          lVar7 = FUN_0030c7a0(param_2, &piStack_2e0);
          piVar4 = aiStack_2d0;
          if (lVar7 != 0)
            goto LAB_0030de2c;
        }
      }
      else
      {
        cStack_130 = *pcVar18;
        uStack_12f = 0x2e;
        piVar4[1] = 2;
        *piVar4 = (int)&cStack_130;
        piVar4 = piVar4 + 2;
        iStack_2dc = iStack_2dc + 1;
        iStack_2d8 = iStack_2d8 + 2;
        if (7 < iStack_2dc)
        {
          lVar7 = FUN_0030c7a0(param_2, &piStack_2e0);
          piVar4 = aiStack_2d0;
          if (lVar7 != 0)
            goto LAB_0030de2c;
        }
        lVar7 = FUN_0030b018(uStack_f0, 0);
        iVar16 = iStack_110;
        if (lVar7 != 0)
        {
          *piVar4 = (int)(pcVar18 + 1);
          piVar4[1] = iVar16 + -1;
          iStack_2d8 = iStack_2d8 + iStack_110 + -1;
          goto joined_r0x0030dbb4;
        }
        iVar16 = iStack_110 + -1;
        if (0 < iVar16)
        {
          uVar13 = 0x350000;
          if (0x10 < iVar16)
          {
            piVar4[1] = 0x10;
            while (true)
            {
              *piVar4 = (int)"0000000000000000Inf";
              piVar4 = piVar4 + 2;
              iStack_2d8 = iStack_2d8 + 0x10;
              iStack_2dc = iStack_2dc + 1;
              if (7 < iStack_2dc)
              {
                iStack_d0 = (int)uVar13;
                uStack_cc = (undefined4)((ulong)uVar13 >> 0x20);
                lVar7 = FUN_0030c7a0(param_2, &piStack_2e0);
                uVar13 = CONCAT44(uStack_cc, iStack_d0);
                if (lVar7 != 0)
                  goto LAB_0030de2c;
                piVar4 = aiStack_2d0;
              }
              iVar16 = iVar16 + -0x10;
              if (iVar16 < 0x11)
                break;
              piVar4[1] = 0x10;
            }
          }
          piVar4[1] = iVar16;
          *piVar4 = (int)uVar13 + 0x1b60;
          iStack_2d8 = iStack_2d8 + iVar16;
          goto joined_r0x0030dbb4;
        }
      }
      iVar16 = iStack_e8;
      *piVar4 = (int)auStack_2f0;
      piVar4[1] = iVar16;
      iStack_2d8 = iStack_2d8 + iVar16;
      goto LAB_0030dccc;
    }
    lVar7 = FUN_0030b018(uStack_f0, 0);
    if (lVar7 == 0)
    {
      piVar4[1] = 1;
      *piVar4 = (int)&DAT_00351bd8;
      piVar4 = piVar4 + 2;
      iStack_2dc = iStack_2dc + 1;
      iStack_2d8 = iStack_2d8 + 1;
      if (7 < iStack_2dc)
      {
        lVar7 = FUN_0030c7a0(param_2, &piStack_2e0);
        piVar4 = aiStack_2d0;
        if (lVar7 != 0)
          goto LAB_0030de2c;
      }
      if ((int)pcStack_114 < iStack_110)
      {
        piVar4[1] = 1;
      }
      else
      {
        if ((uStack_104 & 1) == 0)
          goto LAB_0030dcfc;
        piVar4[1] = 1;
      }
      *piVar4 = iStack_f8;
      piVar4 = piVar4 + 2;
      iStack_2d8 = iStack_2d8 + 1;
      iStack_2dc = iStack_2dc + 1;
      if (7 < iStack_2dc)
      {
        lVar7 = FUN_0030c7a0(param_2, &piStack_2e0);
        piVar4 = aiStack_2d0;
        if (lVar7 != 0)
          goto LAB_0030de2c;
      }
      iVar16 = iStack_110 + -1;
      if (iVar16 < 1)
        goto LAB_0030dcfc;
      uVar13 = 0x350000;
      if (0x10 < iVar16)
      {
        piVar4[1] = 0x10;
        while (true)
        {
          *piVar4 = (int)"0000000000000000Inf";
          piVar4 = piVar4 + 2;
          iStack_2d8 = iStack_2d8 + 0x10;
          iStack_2dc = iStack_2dc + 1;
          if (7 < iStack_2dc)
          {
            iStack_d0 = (int)uVar13;
            uStack_cc = (undefined4)((ulong)uVar13 >> 0x20);
            lVar7 = FUN_0030c7a0(param_2, &piStack_2e0);
            uVar13 = CONCAT44(uStack_cc, iStack_d0);
            if (lVar7 != 0)
              goto LAB_0030de2c;
            piVar4 = aiStack_2d0;
          }
          iVar16 = iVar16 + -0x10;
          if (iVar16 < 0x11)
            break;
          piVar4[1] = 0x10;
        }
      }
      piVar4[1] = iVar16;
      *piVar4 = (int)uVar13 + 0x1b60;
      piVar4 = piVar4 + 2;
      iStack_2dc = iStack_2dc + 1;
      iStack_2d8 = iStack_2d8 + iVar16;
      bVar1 = iStack_2dc < 8;
      goto LAB_0030dcdc;
    }
    if ((int)pcStack_114 < 1)
    {
      piVar4[1] = 1;
      *piVar4 = (int)&DAT_00351bd8;
      piVar4 = piVar4 + 2;
      iStack_2dc = iStack_2dc + 1;
      iStack_2d8 = iStack_2d8 + 1;
      if (7 < iStack_2dc)
      {
        lVar7 = FUN_0030c7a0(param_2, &piStack_2e0);
        piVar4 = aiStack_2d0;
        if (lVar7 != 0)
          goto LAB_0030de2c;
      }
      piVar4[1] = 1;
      *piVar4 = iStack_f8;
      piVar4 = piVar4 + 2;
      iStack_2d8 = iStack_2d8 + 1;
      iStack_2dc = iStack_2dc + 1;
      if (7 < iStack_2dc)
      {
        lVar7 = FUN_0030c7a0(param_2, &piStack_2e0);
        piVar4 = aiStack_2d0;
        if (lVar7 != 0)
          goto LAB_0030de2c;
      }
      iVar16 = -(int)pcStack_114;
      if ((int)pcStack_114 < 0)
      {
        uVar13 = 0x350000;
        if (0x10 < iVar16)
        {
          piVar4[1] = 0x10;
          while (true)
          {
            *piVar4 = (int)"0000000000000000Inf";
            piVar4 = piVar4 + 2;
            iStack_2d8 = iStack_2d8 + 0x10;
            iStack_2dc = iStack_2dc + 1;
            if (7 < iStack_2dc)
            {
              iStack_d0 = (int)uVar13;
              uStack_cc = (undefined4)((ulong)uVar13 >> 0x20);
              lVar7 = FUN_0030c7a0(param_2, &piStack_2e0);
              uVar13 = CONCAT44(uStack_cc, iStack_d0);
              if (lVar7 != 0)
                goto LAB_0030de2c;
              piVar4 = aiStack_2d0;
            }
            iVar16 = iVar16 + -0x10;
            if (iVar16 < 0x11)
              break;
            piVar4[1] = 0x10;
          }
        }
        piVar4[1] = iVar16;
        *piVar4 = (int)uVar13 + 0x1b60;
        piVar4 = piVar4 + 2;
        iStack_2dc = iStack_2dc + 1;
        iStack_2d8 = iStack_2d8 + iVar16;
        if (7 < iStack_2dc)
        {
          lVar7 = FUN_0030c7a0(param_2, &piStack_2e0);
          piVar4 = aiStack_2d0;
          if (lVar7 != 0)
            goto LAB_0030de2c;
        }
      }
      iVar16 = iStack_110;
      *piVar4 = (int)pcVar18;
      piVar4[1] = iVar16;
      iStack_2d8 = iStack_2d8 + iStack_110;
    }
    else
    {
      if (iStack_110 <= (int)pcStack_114)
      {
        piVar4[1] = iStack_110;
        *piVar4 = (int)pcVar18;
        piVar4 = piVar4 + 2;
        iStack_2dc = iStack_2dc + 1;
        iStack_2d8 = iStack_2d8 + iStack_110;
        if (7 < iStack_2dc)
        {
          lVar7 = FUN_0030c7a0(param_2, &piStack_2e0);
          piVar4 = aiStack_2d0;
          if (lVar7 != 0)
            goto LAB_0030de2c;
        }
        iVar16 = (int)pcStack_114 - iStack_110;
        if (0 < iVar16)
        {
          uVar13 = 0x350000;
          if (0x10 < iVar16)
          {
            piVar4[1] = 0x10;
            while (true)
            {
              *piVar4 = (int)"0000000000000000Inf";
              piVar4 = piVar4 + 2;
              iStack_2d8 = iStack_2d8 + 0x10;
              iStack_2dc = iStack_2dc + 1;
              if (7 < iStack_2dc)
              {
                iStack_d0 = (int)uVar13;
                uStack_cc = (undefined4)((ulong)uVar13 >> 0x20);
                lVar7 = FUN_0030c7a0(param_2, &piStack_2e0);
                uVar13 = CONCAT44(uStack_cc, iStack_d0);
                if (lVar7 != 0)
                  goto LAB_0030de2c;
                piVar4 = aiStack_2d0;
              }
              iVar16 = iVar16 + -0x10;
              if (iVar16 < 0x11)
                break;
              piVar4[1] = 0x10;
            }
          }
          piVar4[1] = iVar16;
          *piVar4 = (int)uVar13 + 0x1b60;
          piVar4 = piVar4 + 2;
          iStack_2dc = iStack_2dc + 1;
          iStack_2d8 = iStack_2d8 + iVar16;
          if (7 < iStack_2dc)
          {
            lVar7 = FUN_0030c7a0(param_2, &piStack_2e0);
            piVar4 = aiStack_2d0;
            if (lVar7 != 0)
              goto LAB_0030de2c;
          }
        }
        if ((uStack_104 & 1) == 0)
          goto LAB_0030dcfc;
        piVar4[1] = 1;
        *piVar4 = (int)&DAT_00351be0;
        piVar4 = piVar4 + 2;
        iStack_2dc = iStack_2dc + 1;
        iStack_2d8 = iStack_2d8 + 1;
        bVar1 = iStack_2dc < 8;
        goto LAB_0030dcdc;
      }
      piVar4[1] = (int)pcStack_114;
      *piVar4 = (int)pcVar18;
      piVar4 = piVar4 + 2;
      iStack_2dc = iStack_2dc + 1;
      iStack_2d8 = iStack_2d8 + (int)pcStack_114;
      if (7 < iStack_2dc)
      {
        lVar7 = FUN_0030c7a0(param_2, &piStack_2e0);
        piVar4 = aiStack_2d0;
        if (lVar7 != 0)
          goto LAB_0030de2c;
      }
      pcVar10 = pcStack_114;
      piVar4[1] = 1;
      *piVar4 = (int)&DAT_00351be0;
      iStack_2d8 = iStack_2d8 + 1;
      iStack_2dc = iStack_2dc + 1;
      piVar4 = piVar4 + 2;
      if (7 < iStack_2dc)
      {
        lVar7 = FUN_0030c7a0(param_2, &piStack_2e0);
        piVar4 = aiStack_2d0;
        if (lVar7 != 0)
          goto LAB_0030de2c;
      }
      iVar16 = iStack_110;
      pcVar6 = pcStack_114;
      *piVar4 = (int)(pcVar18 + (int)pcVar10);
      piVar4[1] = iVar16 - (int)pcVar6;
      iStack_2d8 = iStack_2d8 + (iStack_110 - (int)pcStack_114);
    }
    iStack_2dc = iStack_2dc + 1;
    piVar4 = (int *)((int)piVar4 + 8);
    if (iStack_2dc < 8)
      goto LAB_0030dcfc;
  }
  lVar7 = FUN_0030c7a0(param_2, &piStack_2e0);
  piVar4 = aiStack_2d0;
  if (lVar7 != 0)
  {
  LAB_0030de2c:
    uVar9 = *(ushort *)(iVar5 + 0xc);
    goto LAB_0030de30;
  }
LAB_0030dcfc:
  if ((uStack_104 & 4) != 0)
  {
    iVar16 = (int)pcStack_fc - (int)pcStack_e0;
    if (0 < iVar16)
    {
      uVar13 = 0x350000;
      if (0x10 < iVar16)
      {
        piVar4[1] = 0x10;
        while (true)
        {
          *piVar4 = (int)"                0000000000000000Inf";
          piVar4 = piVar4 + 2;
          iStack_2d8 = iStack_2d8 + 0x10;
          iStack_2dc = iStack_2dc + 1;
          if (7 < iStack_2dc)
          {
            uStack_b0 = (undefined4)uVar13;
            uStack_ac = (undefined4)((ulong)uVar13 >> 0x20);
            lVar7 = FUN_0030c7a0(param_2, &piStack_2e0);
            uVar13 = CONCAT44(uStack_ac, uStack_b0);
            if (lVar7 != 0)
              goto LAB_0030de2c;
            piVar4 = aiStack_2d0;
          }
          iVar16 = iVar16 + -0x10;
          if (iVar16 < 0x11)
            break;
          piVar4[1] = 0x10;
        }
      }
      piVar4[1] = iVar16;
      *piVar4 = (int)uVar13 + 0x1b50;
      iStack_2dc = iStack_2dc + 1;
      iStack_2d8 = iStack_2d8 + iVar16;
      if ((7 < iStack_2dc) && (lVar7 = FUN_0030c7a0(param_2, &piStack_2e0), lVar7 != 0))
      {
        uVar9 = *(ushort *)(iVar5 + 0xc);
        goto LAB_0030de30;
      }
    }
  }
  pcVar10 = pcStack_fc;
  if ((int)pcStack_fc <= (int)pcStack_e0)
  {
    pcVar10 = pcStack_e0;
  }
  iStack_100 = iStack_100 + (int)pcVar10;
  if ((iStack_2d8 != 0) && (lVar7 = FUN_0030c7a0(param_2, &piStack_2e0), lVar7 != 0))
  {
    uVar9 = *(ushort *)(iVar5 + 0xc);
  LAB_0030de30:
    iVar5 = -1;
    if ((uVar9 & 0x40) == 0)
    {
      iVar5 = iStack_100;
    }
    return iVar5;
  }
  iStack_2dc = 0;
  piVar4 = aiStack_2d0;
  pcVar10 = pcStack_108;
  goto LAB_0030c9d8;
LAB_0030d1f0:
  do
  {
    uVar2 = (uint)uVar14;
    pcVar18 = pcVar18 + -1;
    uVar14 = uVar14 >> 4;
    *pcVar18 = pcStack_dc[uVar2 & 0xf];
  } while (uVar14 != 0);
  goto LAB_0030d21c;
switchD_0030cacc_caseD_31:
  iVar16 = 0;
  do
  {
    pcStack_fc = (char *)(iVar16 + -0x30 + (int)lVar7);
    cVar3 = *pcStack_108;
    lVar7 = (long)cVar3;
    pcStack_108 = pcStack_108 + 1;
    iVar16 = (int)pcStack_fc * 10;
  } while ((int)cVar3 - 0x30U < 10);
  goto LAB_0030caac;
switchD_0030cacc_caseD_20:
  if (acStack_11f[0] == '\0')
  {
    acStack_11f[0] = ' ';
  }
  goto LAB_0030caa0;
}
