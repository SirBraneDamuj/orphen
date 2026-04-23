/*
 * Opcode 0xEB — set_npc_focus_entity
 *
 * Original: FUN_00265840
 *
 * Reads expr `pool_index` and points the NPC focus pointer
 * (puGpffffb0d8) at &entity_pool[pool_index*0xEC]. Subsequent NPC
 * follower opcodes (0xEC..0xF4) operate on this entity.
 */

extern unsigned short *puGpffffb0d8;
extern unsigned short  DAT_0058beb0[];   /* entity_pool */
extern void script_eval_expression(void *out);

int op_0xEB_set_npc_focus_entity(void) {
    int args[4];
    script_eval_expression(args);
    puGpffffb0d8 = &DAT_0058beb0[args[0] * 0xEC / 2];
    return 0;
}
