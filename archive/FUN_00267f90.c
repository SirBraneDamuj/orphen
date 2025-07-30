
uint *FUN_00267f90(uint param_1)

{
  uint uVar1;
  uint *puVar2;

  if (puGpffffbdcc[1] != 0xffffffff)
  {
    puVar2 = puGpffffbdcc;
    if (puGpffffbdcc[1] == (param_1 & 0x7fffffff))
    {
    LAB_00267fec:
      if (puVar2[1] != 0xffffffff)
      {
        return puVar2 + 2;
      }
    }
    else
    {
      uVar1 = *puGpffffbdcc;
      while (true)
      {
        puVar2 = (uint *)((int)puVar2 + (uVar1 & 0xfffffffc) + 8);
        if (puVar2[1] == 0xffffffff)
          break;
        if (puVar2[1] == (param_1 & 0x7fffffff))
          goto LAB_00267fec;
        uVar1 = *puVar2;
      }
    }
  }
  return (uint *)0x0;
}
