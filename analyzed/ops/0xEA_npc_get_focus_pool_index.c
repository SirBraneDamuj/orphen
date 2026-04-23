/*
 * Opcode 0xEA — npc_get_focus_pool_index
 *
 * Original: FUN_00265818 (LAB before manual function creation)
 *
 * Returns the entity-pool index of the current focus_npc:
 *   (focus_npc - entity_pool_base) * modular_inverse(0xEC) >> 3
 * The constant -0x5f75270d is the modular inverse of 0xEC*8 used elsewhere
 * to recover indices into entity_pool (DAT_0058beb0, stride 0xEC).
 */

extern int iGpffffb0d8;  /* focus_npc pointer */

int op_0xEA_npc_get_focus_pool_index(void)
{
    return (iGpffffb0d8 - 0x58beb0) * -0x5f75270d >> 3;
}
