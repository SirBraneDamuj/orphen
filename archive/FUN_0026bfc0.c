
void FUN_0026bfc0(undefined8 param_1, undefined8 param_2, undefined8 param_3, undefined8 param_4,
                  undefined8 param_5, undefined8 param_6, undefined8 param_7, undefined8 param_8)

{
  char cVar1;
  char *pcVar2;
  undefined8 uStack_38;
  undefined8 uStack_30;
  undefined8 uStack_28;
  undefined8 uStack_20;
  undefined8 uStack_18;
  undefined8 uStack_10;
  undefined8 uStack_8;

  pcVar2 = &DAT_00573538;
  uStack_38 = param_2;
  uStack_30 = param_3;
  uStack_28 = param_4;
  uStack_20 = param_5;
  uStack_18 = param_6;
  uStack_10 = param_7;
  uStack_8 = param_8;
  FUN_0030e0f8(0x573538, param_1, &uStack_38);
  cVar1 = DAT_00573538;
  if (DAT_00573538 != '\0')
  {
    while (true)
    {
      *pcVar2 = cVar1 + '\x01';
      pcVar2 = pcVar2 + 1;
      if (*pcVar2 == '\0')
        break;
      cVar1 = *pcVar2;
    }
  }
  FUN_0030c0c0(0x573538);
  uGpffffb66c = 1;
  uGpffffb66a = 1;
  FUN_002037a8(0x26bf40);
  do
  {
    /* WARNING: Do nothing block with infinite loop */
  } while (true);
}
