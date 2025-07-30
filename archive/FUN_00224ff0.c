
undefined4 FUN_00224ff0(void)

{
  int iVar1;
  undefined4 uVar2;
  long lVar3;
  undefined8 uVar4;
  int *piVar5;
  undefined4 *puVar6;

  if (sGpffffb0ec != 0)
  {
    return 0;
  }
  if (cGpffffb656 == '\x02')
  {
    return 0;
  }
  if ((cGpffffb656 == '\x01') && (iGpffffb65c = iGpffffb65c - iGpffffb64c, iGpffffb65c < 1))
  {
    FUN_0025bc30();
    FUN_0025d1c0(1, 0xc, 0);
    iGpffffb27c = 2;
    cGpffffb656 = 2;
    return 0;
  }
  lVar3 = FUN_00266368(0x508);
  if (lVar3 != 0)
  {
    return 0;
  }
  if (iGpffffb0e4 != 0)
  {
    return 0;
  }
  if (iGpffffb27c != 0)
  {
    return 0;
  }
  if ((0 < DAT_0058c7e8) && (cGpffffb656 == '\0'))
  {
    return 0;
  }
  if (cGpffffb663 == '\0')
  {
    if (iGpffffb284 == 0)
    {
      return 0;
    }
    if (iGpffffb284 == 0xc)
    {
      return 0;
    }
    if (iGpffffb284 == 0xd)
    {
      return 0;
    }
  }
  else if (iGpffffb288 == 0x1f)
  {
    return 0;
  }
  if (cGpffffb6d0 != '\0')
  {
    return 0;
  }
  if (cGpffffb656 == '\x01')
  {
    uVar4 = FUN_0025b9e8(0x26);
    iVar1 = FUN_00238e68(uVar4, 0x20);
    FUN_00238608(-iVar1 / 2, 0x10, uVar4, 0xffffffff80808080, 0x20, 0x20);
  }
  if (iGpffffadbc == 2)
  {
    if ((uGpffffb686 & 0x800) == 0)
    {
      return 0;
    }
    if (((uGpffffb668 & 0x80) != 0) && (cGpffffb66a != '\0'))
    {
      bGpffffb66c = bGpffffb66c ^ 1;
    }
    iVar1 = 0;
    piVar5 = (int *)&gp0xffffbc58;
    do
    {
      if (-1 < *piVar5)
      {
        FUN_002063c8(iVar1, 0x19);
      }
      iVar1 = iVar1 + 1;
      piVar5 = piVar5 + 1;
    } while (iVar1 < 2);
    uGpffffaf26 = 0;
  }
  else
  {
    if ((uGpffffb686 & 0x840) == 0)
    {
      if (cGpffffb663 != '\0')
      {
        return 0;
      }
      if (iGpffffb0e4 != 0)
      {
        return 0;
      }
      if (DAT_0058bf10 != 0)
      {
        return 0;
      }
      lVar3 = FUN_00237c60();
      if (lVar3 != 0)
      {
        return 0;
      }
      if (iGpffffadbc == 2)
      {
        return 0;
      }
      if ((uGpffffb686 & 0x5000) != 0)
      {
        iGpffffadbc = 4;
        FUN_00231a98();
        return 0;
      }
      if ((uGpffffb686 & 0x8000) == 0)
      {
        return 0;
      }
      lVar3 = FUN_00266368(0x512);
      if (lVar3 != 0)
      {
        FUN_00213ef0();
        DAT_0058cba0 = DAT_0058cba0 | 1;
        iGpffffadbc = 0xc;
        return 0;
      }
      return 0;
    }
    if (cGpffffb656 == '\x01')
    {
      FUN_0025d1c0(1, 0xc, 0);
      cGpffffb656 = 2;
      uGpffffb662 = 0xff;
      iGpffffb27c = 2;
      FUN_002241d8();
      return 0;
    }
    iVar1 = 0;
    if ((uGpffffb686 & 0x800) != 0)
    {
      puVar6 = (undefined4 *)&gp0xffffbc58;
      do
      {
        lVar3 = FUN_00206218(iVar1);
        if (lVar3 < 1)
        {
          *puVar6 = 0xffffffff;
        }
        else
        {
          lVar3 = FUN_00206238(iVar1);
          if (lVar3 == 0)
          {
            uVar2 = FUN_002061f8(iVar1);
            *puVar6 = uVar2;
            FUN_00206260(iVar1, 0x19, 500);
          }
          else
          {
            *puVar6 = 0xffffffff;
          }
        }
        iVar1 = iVar1 + 1;
        puVar6 = puVar6 + 1;
      } while (iVar1 < 2);
      iGpffffadbc = 1;
      if ((cGpffffb657 == '\0') && (iGpffffadbc = 1, cGpffffb663 == '\0'))
      {
        iGpffffadbc = 2;
      }
      FUN_002256b0();
      return 1;
    }
  }
  return 1;
}
