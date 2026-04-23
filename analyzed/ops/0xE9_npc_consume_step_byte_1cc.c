/*
 * Opcode 0xE9 — npc_consume_step_byte_1cc
 *
 * Original: FUN_00265d88 (LAB before manual function creation)
 *
 * Reads the byte at focus_npc[+0x1CC], clears it, and returns the previous
 * value. Used by NPC scripts to one-shot consume a flag deposited by the
 * engine (see also 0xED `npc_get_step_byte` at +0x1BC, 0xEC writer).
 */

extern int iGpffffb0d8;  /* focus_npc pointer (alias of puGpffffb0d8) */

unsigned char op_0xE9_npc_consume_step_byte_1cc(void)
{
    unsigned char prev = *(unsigned char *)(iGpffffb0d8 + 0x1cc);
    *(unsigned char *)(iGpffffb0d8 + 0x1cc) = 0;
    return prev;
}
