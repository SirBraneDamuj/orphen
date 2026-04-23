/*
 * Opcode 0xB2 — set_entity_anim_rate
 *
 * Original: FUN_002638d8
 *
 * Args: expr `selector`, expr `anim_slot`, expr `rate`.
 *
 * Selects the entity, builds a zero-filled 0x1C-byte snapshot and reads
 * the entity's current animation snapshot into it (anim_snapshot_read
 * uses entity[+0xA0] as the animation channel). Overwrites the rate float
 * at offset +0x10 with `rate / fGpffff8d34` (the script-to-rate scale)
 * and reapplies the snapshot.
 */

#include <stdint.h>

extern int selected_entity_as_int;                   /* iGpffffb0d4 */
extern void script_eval_expression(void *out);       /* FUN_0025c258 */
extern void script_select_entity(int i, void *prev); /* FUN_0025d6c0 */
extern void mem_zero(void *ptr, int n);              /* FUN_0030bfac */
extern void anim_snapshot_read(void *entity, int anim_slot,
                               short channel, int zero, void *buf); /* FUN_0020da68 */
extern void anim_snapshot_apply(void *entity, int anim_slot,
                                void *buf, int zero); /* FUN_0020d8c0 */
extern float fGpffff8d34;

int op_0xB2_set_entity_anim_rate(void)
{
  int selector, anim_slot, rate;
  int prev_entity = selected_entity_as_int;
  unsigned char snapshot[0x1C];
  script_eval_expression(&selector);
  script_eval_expression(&anim_slot);
  script_eval_expression(&rate);
  script_select_entity(selector, (void *)(intptr_t)prev_entity);
  mem_zero(snapshot, 0x1C);
  void *entity = (void *)(intptr_t)selected_entity_as_int;
  anim_snapshot_read(entity, anim_slot,
                     *(short *)((intptr_t)entity + 0xA0), 0, snapshot);
  *(float *)(snapshot + 0x10) = (float)rate / fGpffff8d34;
  anim_snapshot_apply(entity, anim_slot, snapshot, 0);
  return 0;
}
