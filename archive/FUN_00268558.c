
void FUN_00268558(char *param_1, char *param_2)

{
  char cVar1;

  do
  {
    cVar1 = *param_2;
    param_2 = param_2 + 1;
    *param_1 = cVar1;
    param_1 = param_1 + 1;
  } while (cVar1 != '\0');
  return;
}
