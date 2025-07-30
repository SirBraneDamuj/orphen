
int FUN_002320a8(int param_1)

{
  undefined1 uVar1;
  int iVar2;
  undefined8 uVar3;
  long lVar4;
  int *piVar5;
  int iVar6;
  int iVar7;
  long lVar8;

  if (param_1 == 0)
  {
    lVar8 = 0;
    iVar7 = 0;
    DAT_0031c514 = 0x4b;
    DAT_0031c510 = DAT_0031c468 + -0x1e;
    do
    {
      uVar3 = FUN_0025b9e8(iVar7 + 0x50);
      iVar7 = iVar7 + 1;
      lVar4 = FUN_00238e68(uVar3, 0x14);
      if (lVar8 < lVar4)
      {
        lVar8 = lVar4;
      }
    } while (iVar7 < 5);
    DAT_0031c504 = (int)lVar8 + 0x20;
    param_1 = 1;
    DAT_00355c28 = 4;
  }
  else if (param_1 == 1)
  {
    iVar7 = DAT_003555bc + 3;
    if (-1 < DAT_003555bc)
    {
      iVar7 = DAT_003555bc;
    }
    piVar5 = &DAT_0031c468;
    iVar6 = 0;
    iVar2 = iVar7 >> 2;
    if (DAT_0031c514 < iVar7 >> 2)
    {
      iVar2 = DAT_0031c514;
    }
    do
    {
      iVar6 = iVar6 + -1;
      *piVar5 = *piVar5 + iVar2;
      piVar5 = piVar5 + 6;
    } while (-1 < iVar6);
    iVar7 = 5;
    piVar5 = &DAT_0031c480;
    do
    {
      iVar7 = iVar7 + -1;
      *piVar5 = *piVar5 - iVar2;
      piVar5 = piVar5 + 6;
    } while (-1 < iVar7);
    DAT_0031c514 = DAT_0031c514 - iVar2;
    if (DAT_0031c514 < 1)
    {
      param_1 = 2;
    }
  }
  else if (param_1 == 3)
  {
    iVar7 = DAT_003555bc + 3;
    if (-1 < DAT_003555bc)
    {
      iVar7 = DAT_003555bc;
    }
    iVar7 = iVar7 >> 2;
    if (DAT_0031c468 - DAT_0031c480 < 0x1f)
    {
      FUN_0023b8e0(0x355608);
      return 0;
    }
    piVar5 = &DAT_0031c480;
    iVar2 = 5;
    do
    {
      iVar2 = iVar2 + -1;
      *piVar5 = *piVar5 + iVar7;
      piVar5 = piVar5 + 6;
    } while (-1 < iVar2);
    iVar2 = 0;
    piVar5 = &DAT_0031c468;
    do
    {
      iVar2 = iVar2 + -1;
      *piVar5 = *piVar5 - iVar7;
      piVar5 = piVar5 + 6;
    } while (-1 < iVar2);
    piVar5 = &DAT_0031c468;
    if ((DAT_0031c468 - DAT_0031c480) - iVar7 < 0x1e)
    {
      iVar2 = 6;
      iVar7 = DAT_0031c510 + 0x1e;
      do
      {
        *piVar5 = iVar7;
        iVar2 = iVar2 + -1;
        piVar5 = piVar5 + 6;
        iVar7 = iVar7 + -0x1e;
      } while (-1 < iVar2);
    }
  }
  else
  {
    if (param_1 != 2)
    {
      return param_1;
    }
    lVar8 = FUN_0023b9f8(0x5000, 1);
    if (lVar8 != 0)
    {
      if ((DAT_003555f4 & 0x1000) == 0)
      {
        if (((DAT_003555f4 & 0x4000) != 0) && (DAT_00355c28 = DAT_00355c28 + 1, 4 < DAT_00355c28))
        {
          DAT_00355c28 = 0;
        }
      }
      else
      {
        DAT_00355c28 = DAT_00355c28 + -1;
        if (DAT_00355c28 < 0)
        {
          DAT_00355c28 = 4;
        }
      }
      FUN_002256c0();
    }
    if (DAT_00355c28 < 4)
    {
      if ((DAT_003555f6 & 0xf0) != 0)
      {
        uVar1 = (&DAT_0035560c)[DAT_00355c28];
        if ((DAT_003555f6 & 0x10) == 0)
        {
          if ((DAT_003555f6 & 0x20) == 0)
          {
            if ((DAT_003555f6 & 0x40) == 0)
            {
              if ((DAT_003555f6 & 0x80) != 0)
              {
                FUN_00232058(uVar1, 0x80);
              }
            }
            else
            {
              FUN_00232058(uVar1, 0x40);
            }
          }
          else
          {
            FUN_00232058(uVar1, 0x20);
          }
        }
        else
        {
          FUN_00232058(uVar1, 0x10);
        }
        FUN_002256c0();
      }
    }
    else if ((DAT_003555f6 & 0x40) != 0)
    {
      DAT_00355c28 = 0;
      FUN_002256b0();
      return 3;
    }
  }
  iVar7 = DAT_0031c468;
  if (param_1 == 2)
  {
    uVar3 = FUN_0025b9e8(0x55);
    iVar2 = FUN_00238e68(uVar3, 0x14);
    FUN_00238608(0x130 - iVar2, iVar7 + -0x78, uVar3, 0xffffffff80808080, 0x14, 0x16);
    uVar3 = FUN_0025b9e8(0x56);
    iVar2 = FUN_00238e68(uVar3, 0x14);
    FUN_00238608(0x130 - iVar2, iVar7 + -0x8e, uVar3, 0xffffffff80808080, 0x14, 0x16);
    FUN_00231e60(4, 0x50);
    FUN_00231e60(5, 0x52);
    FUN_00231e60(6, 0x51);
    FUN_00231e60(7, 0x53);
    FUN_00231e60(8, 0x54);
  }
  return param_1;
}
