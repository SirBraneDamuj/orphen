/*
 * Opcode 0xED — npc_get_step_byte
 *
 * Original: FUN_002658b0 (LAB before manual function creation)
 *
 * Returns the byte at focus_npc[+0x1BC] — the field written by opcode 0xEC
 * (set_npc_step_byte, FUN_00265880).
 */

extern int iGpffffb0d8; /* focus_npc pointer */

unsigned char op_0xED_npc_get_step_byte(void)
{
  return *(unsigned char *)(iGpffffb0d8 + 0x1bc);
}
