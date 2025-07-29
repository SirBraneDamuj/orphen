
void FUN_00268500(char *param_1, char *param_2)

{
  char cVar1;

  if (*param_1 == '\0')
  {
    cVar1 = *param_2;
    while (true)
    {
      param_2 = param_2 + 1;
      *param_1 = cVar1;
      param_1 = param_1 + 1;
      if (cVar1 == '\0')
        break;
    LAB_00268530:
      cVar1 = *param_2;
    }
    return;
  }
  param_1 = param_1 + 1;
  do
  {
    if (*param_1 == '\0')
      goto LAB_00268530;
    param_1 = param_1 + 1;
  } while (true);
}
