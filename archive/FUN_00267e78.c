
void FUN_00267e78(undefined4 *param_1, uint param_2)

{
  uint uVar1;

  uVar1 = (uint)param_1 & 3;
  if (uVar1 != 0)
  {
    param_2 = param_2 - uVar1;
    while (uVar1 = uVar1 - 1, uVar1 != 0xffffffff)
    {
      *(undefined1 *)param_1 = 0;
      param_1 = (undefined4 *)((int)param_1 + 1);
    }
  }
  uVar1 = param_2 >> 2;
  param_2 = param_2 & 3;
  while (uVar1 = uVar1 - 1, uVar1 != 0xffffffff)
  {
    *param_1 = 0;
    param_1 = param_1 + 1;
  }
  if (param_2 != 0)
  {
    while (param_2 = param_2 - 1, param_2 != 0xffffffff)
    {
      *(undefined1 *)param_1 = 0;
      param_1 = (undefined4 *)((int)param_1 + 1);
    }
  }
  return;
}
