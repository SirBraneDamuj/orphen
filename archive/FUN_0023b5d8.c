
void FUN_0023b5d8(long param_1)

{
  ushort uVar1;
  uint uVar2;
  int iVar3;
  ushort uVar4;
  uint uVar5;
  undefined4 uVar6;
  float fVar7;

  uVar1 = DAT_003555fe;
  if (DAT_003555ac != DAT_00354e68)
  {
    DAT_00354e68 = DAT_003555ac;
    FUN_0023af50();
  }
  if (DAT_00571a00 == '\0')
  {
    uVar5 = ~(uint)CONCAT11(DAT_00571a02, DAT_00571a03) & 0xffff;
  }
  else
  {
    uVar5 = 0;
  }
  if (DAT_00354e50 != 0)
  {
    if (DAT_00571a20 == '\0')
    {
      uVar2 = ~(uint)CONCAT11(DAT_00571a22, DAT_00571a23) & 0xffff;
    }
    else
    {
      uVar2 = 0;
    }
    if (uVar5 == 0)
    {
      if (uVar2 != 0)
      {
        DAT_003555df = '\x01';
        uVar5 = uVar2;
      }
    }
    else
    {
      DAT_003555df = '\0';
    }
  }
  iVar3 = DAT_003555df * 0x20;
  if ((byte)(&DAT_00571a01)[iVar3] >> 4 == 7)
  {
    DAT_003555e0 = -1;
    FUN_0023b3f0(*(undefined1 *)(iVar3 + 0x571a04), *(undefined1 *)(iVar3 + 0x571a05), 0x3555f0,
                 0x3555ec);
    FUN_0023b3f0(*(undefined1 *)(DAT_003555df * 0x20 + 0x571a06),
                 (&DAT_00571a07)[DAT_003555df * 0x20], 0x3555e8, 0x3555e4);
    DAT_003555fe = FUN_0023b4e8(DAT_003555e8, DAT_003555e4);
  }
  else
  {
    DAT_003555e0 = '\0';
    uVar5 = 0;
  }
  uVar4 = (ushort)uVar5;
  if ('\0' < DAT_003555e0)
  {
    if ((uVar5 & 0xf000) == 0)
    {
      DAT_003555e8 = 0;
      DAT_003555e4 = 0.0;
    }
    else if ((DAT_00354e6c & 0xf000) == 0)
    {
      DAT_003555e4 = (float)FUN_0023b358(uVar5);
      DAT_003555e8 = 0x43000000;
    }
    else
    {
      uVar6 = FUN_0023b358(uVar5);
      fVar7 = (float)FUN_0023a320(DAT_003555e4, uVar6, (float)DAT_003555bc * DAT_00352648);
      DAT_003555e4 = DAT_003555e4 + fVar7;
      DAT_003555e8 = 0x43000000;
    }
    DAT_003555f0 = 0;
    DAT_003555ec = 0;
    DAT_00354e6c = uVar4;
  }
  DAT_00354e52 = 0;
  DAT_003555f4 = uVar4;
  if (param_1 != 0)
  {
    DAT_003555f6 = (ushort)(uVar5 & ~(uint)DAT_00354e56);
    uVar2 = (uint)(byte)(&DAT_00571a50)[uVar5 & 0xff] | uVar5 & 0xff00;
    DAT_003555f8 = (undefined2)uVar2;
    DAT_00355614 = DAT_00355614 + 1 & 0x3f;
    DAT_003555fa = (ushort)(byte)(&DAT_00571a50)[uVar5 & ~(uint)DAT_00354e56 & 0xff] |
                   DAT_003555f6 & 0xff00;
    DAT_00355618 = DAT_00355618 + 1;
    DAT_00355600 = DAT_003555fe & ~uVar1;
    DAT_00354e54 = 0;
    DAT_00354e56 = uVar4;
    *(uint *)(&DAT_00342a70 + DAT_00355614 * 4) = uVar2 << 0x10 | (uint)DAT_003555fa;
    DAT_003555fc = 0;
    if (0x40 < DAT_00355618)
    {
      DAT_00355618 = 0x40;
    }
  }
  return;
}
