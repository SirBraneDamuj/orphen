/*
 * Opcode 0x13F — entity_compute_pool_indices
 *
 * Original: FUN_002604a8
 *
 * Reads entity index + 2 work-mem slot indices. Selects entity; if its
 * type (*entity) == 0x28, clears entity[+0x4A], calls FUN_002d2f40 on
 * it, then computes (entity_field_0xCE - 0x58beb0)*-0x5f75270d>>3 and
 * (entity_field_0xCC - 0x58beb0)*... and stores them at
 * work_mem[+slot0*4] and work_mem[+slot1*4]. Otherwise emits diagnostic
 * at 0x34cf68.
 *
 * The constant -0x5f75270d is the modular inverse of 0xEC * 8, used to
 * convert an entity-pool address back into its index (entity stride 0xEC).
 */

#include <stdint.h>

extern unsigned short *DAT_00355044; /* selected_entity */
extern int DAT_00355060;             /* work memory base */
extern void script_eval_expression(void *out);
extern void script_select_entity(unsigned int idx, void *ctx);
extern void script_diagnostic(unsigned int msg_addr);
extern void FUN_002d2f40(void *entity);

unsigned int op_0x13F_entity_compute_pool_indices(void)
{
  unsigned short *ctx = DAT_00355044;
  unsigned int idx;
  int slot0, slot1;
  script_eval_expression(&idx);
  script_eval_expression(&slot0);
  script_eval_expression(&slot1);
  script_select_entity(idx, ctx);
  if (*DAT_00355044 == 0x28)
  {
    ((unsigned char *)DAT_00355044)[0x4A] = 0;
    FUN_002d2f40(DAT_00355044);
    int base = DAT_00355060;
    int fceCC = *(int *)((char *)DAT_00355044 + 0xCC);
    *(int *)(slot0 * 4 + DAT_00355060) =
        ((*(int *)((char *)DAT_00355044 + 0xCE) - 0x58beb0) * -0x5f75270d) >> 3;
    *(int *)(slot1 * 4 + base) = ((fceCC - 0x58beb0) * -0x5f75270d) >> 3;
  }
  else
  {
    script_diagnostic(0x34cf68);
  }
  return 0;
}
