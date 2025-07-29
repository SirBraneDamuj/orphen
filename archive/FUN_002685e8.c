
int FUN_002685e8(char *param_1)

{
  char cVar1;
  char *pcVar2;
  int iVar3;

  iVar3 = 0;
  cVar1 = *param_1;
  pcVar2 = param_1 + 1;
  while (cVar1 != '\0')
  {
    iVar3 = iVar3 + 1;
    cVar1 = *pcVar2;
    pcVar2 = pcVar2 + 1;
  }
  return iVar3;
}
