
void FUN_0025bc30(void)

{
  undefined4 *puVar1;
  int iVar2;

  puVar1 = (undefined4 *)(iGpffffbd84 + 0x100);
  if (iGpffffbd84 != 0)
  {
    iVar2 = 0x40;
    do
    {
      *puVar1 = 0;
      iVar2 = iVar2 + -1;
      puVar1 = puVar1 + -1;
    } while (-1 < iVar2);
  }
  return;
}
