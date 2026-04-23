/*
 * Opcode 0xE1 — boot_party_for_battle
 *
 * Original: FUN_00265000
 *
 * No script args. Clears event flag 0x8EE, sets DAT_00354d2c = 0x10
 * (battle start state), respawns the lead via entity_spawn_typed(lead, 10, 1)
 * and walks the seven party_slots: for each slot whose pool index is in
 * 0..0xFF and whose entity type is 0x37 (party member), respawns it with
 * entity_spawn_typed(member, 1, 0) and sets entity[+0x12] = 8 (battle
 * idle anim). Finishes with FUN_002686a0 (some battle-system handoff).
 */

#include <stdint.h>

extern void flag_clear(int flag);                           /* FUN_002663a0 */
extern void entity_spawn_typed(void *e, int kind, int sub); /* FUN_00225bf0 */
extern void FUN_002686a0(void);
extern int DAT_00354d2c;
extern short DAT_00343692[];         /* party slots base */
extern short DAT_0058beb0[];         /* entity_pool */
extern unsigned char DAT_0058bf12[]; /* per-pool battle anim slot, +0x62 */

int op_0xE1_boot_party_for_battle(void)
{
  flag_clear(0x8EE);
  DAT_00354d2c = 0x10;
  entity_spawn_typed(&DAT_0058beb0[0], 10, 1);

  short *slot = &DAT_00343692[0];
  for (int i = 6; i >= 0; i--)
  {
    short pool_idx = *slot;
    if (pool_idx != 0 && pool_idx < 0x100 &&
        DAT_0058beb0[pool_idx * 0xEC / 2] == 0x37)
    {
      entity_spawn_typed(&DAT_0058beb0[pool_idx * 0xEC / 2], 1, 0);
      DAT_0058bf12[pool_idx * 0xEC] = 8;
    }
    slot += 0x14 / 2;
  }
  FUN_002686a0();
  return 0;
}
