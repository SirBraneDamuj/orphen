
void FUN_00268498(byte *param_1, int param_2, undefined8 param_3)

{
  byte bVar1;

  while (true)
  {
    bVar1 = *param_1;
    param_1 = param_1 + 1;
    if (bVar1 == 0)
      break;
    if (bVar1 - 0x20 < 0x60)
    {
      FUN_00268410(bVar1, param_2, param_3);
      param_2 = param_2 + 0xc;
    }
  }
  return;
}
