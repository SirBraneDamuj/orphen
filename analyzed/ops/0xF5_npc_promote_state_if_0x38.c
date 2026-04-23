/*
 * Opcode 0xF5 — npc_promote_state_if_0x38
 *
 * Original: FUN_00265d98 (LAB before manual function creation)
 *
 * If the focus_npc's current state short (focus_npc[+0]) equals 0x38, replace
 * it with the queued state stored at focus_npc[+0x1CE] (psGpffffb0d8[0xE7]).
 * Otherwise no-op. Always returns 0.
 *
 * Effectively a one-way "advance state when we're in idle 0x38" hook used by
 * NPC scripts to commit a previously-queued behaviour transition.
 */

extern short *psGpffffb0d8;  /* focus_npc pointer (short view) */

int op_0xF5_npc_promote_state_if_0x38(void)
{
    if (*psGpffffb0d8 == 0x38) {
        *psGpffffb0d8 = psGpffffb0d8[0xE7];
    }
    return 0;
}
