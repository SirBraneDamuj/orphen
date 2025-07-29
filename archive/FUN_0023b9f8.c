
undefined4 FUN_0023b9f8(ushort param_1, long param_2)

{
  int iVar1;

  iGpffffaf00 = iGpffffaf00 + iGpffffb64c;
  if (0x200 < iGpffffaf00)
  {
    iGpffffaf00 = 0x200;
  }
  if (0x1f < iGpffffaf00)
  {
    FUN_0023b5d8(0);
    if (param_2 != 0)
    {
      uGpffffb684 = uGpffffb684 | uGpffffb68e & param_1;
    }
    if ((uGpffffb684 & param_1) == 0)
    {
      sGpffffaefe = 0;
    }
    else if (0 < iGpffffaf00)
    {
      while (true)
      {
        sGpffffaefe = sGpffffaefe + 1;
        iVar1 = (int)sGpffffaefe;
        if ((iVar1 == 1) || ((0xc < iVar1 && ((iVar1 - 0xdU & 3) == 0))))
          break;
        iGpffffaf00 = iGpffffaf00 + -0x20;
        if (iGpffffaf00 < 1)
        {
          return 0;
        }
      }
      iGpffffaf00 = 0;
      return 1;
    }
  }
  return 0;
}
