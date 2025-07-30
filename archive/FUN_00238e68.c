
/* WARNING: Removing unreachable block (ram,0x00238e98) */
/* WARNING: Removing unreachable block (ram,0x00238ecc) */

short FUN_00238e68(char *param_1, int param_2)

{
  char cVar1;
  short sVar2;
  char *pcVar3;
  short sVar4;
  int iVar5;

  sVar4 = 0;
  cVar1 = *param_1;
  pcVar3 = param_1 + 1;
  while (cVar1 != '\0')
  {
    sVar2 = 0x20;
    if (-1 < cVar1)
    {
      iVar5 = FUN_00238e50();
      sVar2 = (short)((iVar5 * ((param_2 * 100) / 0x16)) / 100);
    }
    sVar4 = sVar4 + sVar2;
    cVar1 = *pcVar3;
    pcVar3 = pcVar3 + 1;
  }
  return sVar4;
}
