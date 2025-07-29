
undefined4 FUN_0026a1b8(void)

{
  bool bVar1;
  int iVar2;
  int iVar3;
  undefined4 uVar4;
  long lVar5;
  int iVar6;
  int iVar7;
  undefined1 auStack_c0[80];

  iVar7 = 0;
  FUN_00268558(auStack_c0, 0x34d8d8);
  if (DAT_003550fc == 1)
  {
    FUN_00268500(auStack_c0, 0x355110);
  }
  else if (DAT_003550fc < 2)
  {
    if (DAT_003550fc == 0)
    {
      FUN_00268500(auStack_c0, 0x355108);
    }
  }
  else if (DAT_003550fc == 2)
  {
    FUN_00268500(auStack_c0, 0x355118);
  }
  else if (DAT_003550fc == 3)
  {
    FUN_00268500(auStack_c0, 0x355120);
  }
  iVar3 = FUN_002685e8(auStack_c0);
  iVar3 = iVar3 * 0xc;
  iVar2 = -(iVar3 >> 1);
  FUN_00268498(auStack_c0, iVar2, 0);
  if (DAT_003550fc == 1)
  {
    iVar6 = 0xe0;
    iVar7 = 800;
  LAB_0026a314:
    bVar1 = iVar6 < DAT_00355100;
  }
  else
  {
    iVar6 = iVar3;
    if (1 < DAT_003550fc)
    {
      if (DAT_003550fc == 2)
      {
        iVar6 = 0x100;
        iVar7 = 0x400;
      }
      else
      {
        if (DAT_003550fc != 3)
        {
          bVar1 = iVar3 < DAT_00355100;
          goto LAB_0026a31c;
        }
        iVar6 = 0x400;
        iVar7 = 0x500;
      }
      goto LAB_0026a314;
    }
    if (DAT_003550fc == 0)
    {
      iVar6 = 0x400;
      iVar7 = 0;
      goto LAB_0026a314;
    }
    bVar1 = iVar3 < DAT_00355100;
  }
LAB_0026a31c:
  if (bVar1)
  {
    DAT_00355100 = iVar6 + -1;
  }
  lVar5 = FUN_00266368(iVar7 + DAT_00355100);
  FUN_0030c1d8(auStack_c0, 0x34d8e8, DAT_00355100, lVar5 != 0);
  FUN_00268498(auStack_c0, iVar2 + 0x10, 0xffffffffffffffec);
  FUN_00268650(iVar2 + -4, 0, iVar3 + 4, 0x14, 0x6000);
  FUN_00268650(iVar2 + -4, 4, iVar3 + 8, 0x30, 0x600000);
  lVar5 = FUN_0023b9f8(0xf00c, 0);
  if (lVar5 == 0)
    goto LAB_0026a474;
  if ((DAT_003555f4 & 0x8000) != 0)
  {
    DAT_003550fc = DAT_003550fc + -1;
    if (DAT_003550fc < 0)
    {
      DAT_003550fc = 3;
    }
    goto LAB_0026a474;
  }
  if ((DAT_003555f4 & 0x2000) != 0)
  {
    DAT_003550fc = DAT_003550fc + 1;
    if (3 < DAT_003550fc)
    {
      DAT_003550fc = 0;
    }
    goto LAB_0026a474;
  }
  if ((DAT_003555f4 & 0x1000) == 0)
  {
    if ((DAT_003555f4 & 0x4000) == 0)
    {
      if ((DAT_003555f4 & 4) == 0)
      {
        if (((DAT_003555f4 & 8) == 0) || (DAT_00355100 = DAT_00355100 + -10, -1 < DAT_00355100))
          goto LAB_0026a474;
        goto LAB_0026a470;
      }
      DAT_00355100 = DAT_00355100 + 10;
      if (DAT_00355100 < iVar6)
        goto LAB_0026a474;
    }
    else
    {
      bVar1 = 0 < DAT_00355100;
      DAT_00355100 = DAT_00355100 + -1;
      if (bVar1)
        goto LAB_0026a474;
    }
    DAT_00355100 = iVar6 + -1;
  }
  else
  {
    bVar1 = DAT_00355100 < iVar6 + -1;
    DAT_00355100 = DAT_00355100 + 1;
    if (bVar1)
      goto LAB_0026a474;
  LAB_0026a470:
    DAT_00355100 = 0;
  }
LAB_0026a474:
  uVar4 = 0;
  if ((DAT_003555f6 & 0x100) == 0)
  {
    if ((DAT_003555f6 & 0x20) != 0)
    {
      if (DAT_003550fc == 1)
      {
        iVar7 = DAT_00355100 + 800;
      }
      else
      {
        iVar7 = DAT_00355100;
        if (1 < DAT_003550fc)
        {
          if (DAT_003550fc == 2)
          {
            iVar7 = DAT_00355100 + 0x400;
          }
          else if (DAT_003550fc == 3)
          {
            iVar7 = DAT_00355100 + 0x500;
          }
        }
      }
      FUN_00266418(iVar7);
    }
    FUN_002686a0();
    uVar4 = 1;
  }
  return uVar4;
}
