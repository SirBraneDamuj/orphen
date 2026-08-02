/*
 * Entity pool allocation and type-descriptor resolution
 * Originals: FUN_00265dc0 (0x00265dc0) pool allocator
 *            FUN_00265e28 (0x00265e28) allocate-and-initialise
 *            FUN_00229c40 (0x00229c40) initialise entity from a type descriptor
 *            FUN_00229980 (0x00229980) resolve type id to descriptor + model record
 *            FUN_00229be8 (0x00229be8) validated wrapper around the above
 *            FUN_002662e0 (0x002662e0) position setter
 *            FUN_00266240 (0x00266240) place a freshly spawned entity
 *
 * This is the machinery behind the script spawn opcodes 0x50, 0x51, 0x52 and
 * 0x65, and behind the lead player, which is simply slot 0.
 *
 * ---------------------------------------------------------------------------
 * The pool: base 0x0058BEB0, stride 0x1D8, 256 slots
 * ---------------------------------------------------------------------------
 *
 * FUN_00265dc0 indexes as `&DAT_0058beb0 + slot * 0xec` where the pointer is
 * `undefined2 *`. That is 0xEC *halfwords*, so the byte stride is 0x1D8 (472),
 * not 0xEC (236). Three independent confirmations:
 *
 *   1. FUN_00229c40 clears the entity with FUN_00267e78(entity, 0x1d8) and
 *      writes fields as far out as +0x195, which does not fit in 236 bytes.
 *   2. FUN_00229c40 recovers the slot index from the pointer with the
 *      divide-by-constant idiom `(byteOffset * -0x5f75270d) >> 3`. That is an
 *      exact division by modular inverse: inverse(0xA08AD8F3) mod 2^32 = 0x3B,
 *      and 0x3B << 3 = 0x1D8.
 *   3. 0x58BEB0 + 256 * 0x1D8 = 0x5A96B0, which is exactly where the per-slot
 *      status array DAT_005a96b0 begins.
 *
 * So the pool is 256 slots of 472 bytes, immediately followed by a 256-byte
 * status array. Status values seen: 0 free, 1 script-spawned (type > 0),
 * 0xFF allocated/reserved (type < 0).
 *
 * Slot 0 is the lead player. The camera reads DAT_0058bed0, which is
 * 0x58BEB0 + 0x20 -- slot 0's world X. Script spawns start at slot 10:
 * every spawn path calls FUN_00265dc0(10, 0xF6), i.e. the range [10, 256).
 * Slots 1..9 are reserved for the party and system entities.
 *
 * ---------------------------------------------------------------------------
 * Type descriptors
 * ---------------------------------------------------------------------------
 *
 * FUN_00229980(context, typeId, out) selects between several static tables by
 * id range and returns a 0x1C-byte descriptor, writing the matching 0x2C-byte
 * model record through `out`:
 *
 *   id == 0x38            replaced by *(short *)(context + 0x1CE) (indirection)
 *   0x7C <= id < 0x1FB    DAT_00319900 + (id-0x7C)*0x1C, models DAT_003214f8
 *   0xFC <= id < 0x1F0    DAT_0031a700 + (id-0xFC)*0x1C, models PTR_DAT_003228c0
 *   0x1F1 <= id < 0x271   descriptor 0x3198E0, model 0x31A95C + id*0x2C
 *   0x272 <= id           map-streamed: FUN_00229688 pulls the record out of the
 *                         currently loaded scene rather than a static table
 *   otherwise             DAT_00318b68 + (id-1)*0x1C, models DAT_0031ee48,
 *                         bounds-checked against iGpffffae04
 *
 * In every static case the model record is `modelBase + descriptor[0] * 0x2C`
 * (written as `ppuVar2 + *psVar4 * 0xb` over a 4-byte pointer type).
 *
 * Descriptor layout, from what FUN_00229c40 copies out of it:
 *
 *   +0x00 s16   index into the 0x2C-byte model record table
 *   +0x02 u8    -> entity +0x133
 *   +0x04 u16   flags -> entity +0x02; bit 0x200 selects the alternate
 *               model-pointer branch, bit 0x200 clear also sets entity flag 0x10
 *   +0x08 f32   collision radius -> entity +0x54, and +0x11C
 *   +0x0C f32   height           -> entity +0x58, and +0x120
 *   +0x10 u32   -> entity +0x80
 *   +0x14 s8    -> entity +0x06
 *   +0x16 u16   -> entity +0x08 (OR 0x20 when typeId < 0x272)
 *   +0x18 u16   -> entity +0x04
 *
 * The radius and height at +0x08/+0x0C are the same fields the field collision
 * code reads back out of the entity, so they are genuine collision dimensions.
 *
 * FUN_00229be8 is the guarded entry point: it rejects id 0x38 and any id above
 * 0x574 with "ER_PARAM [get_pchr]" (strings.json 0x0034c038) before delegating,
 * which puts an upper bound of 0x574 on valid type ids.
 *
 * ---------------------------------------------------------------------------
 * Positioning
 * ---------------------------------------------------------------------------
 *
 * FUN_002662e0(x, y, z, entity) writes +0x20, +0x24, +0x28 and mirrors z into
 * +0x4C. FUN_00227070(x, y, entity) then samples terrain and overwrites +0x4C
 * with the ground height, so +0x4C is "height of the floor under me", seeded to
 * the spawn z until terrain is consulted.
 *
 * Note the axis convention: the terrain query takes (x, y) and the result is a
 * *z*, so +0x28 is the vertical axis, not +0x24.
 *
 * Correction to analyzed/ops/0x54_0x55_set_entity_position.c: it records the
 * terrain height as landing at entity+0x26. The decompiled write is
 * `*(undefined4 *)(puGpffffb0d4 + 0x26)` where puGpffffb0d4 is `undefined2 *`,
 * so the byte offset is 0x4C -- the same halfword-versus-byte confusion as the
 * pool stride above. That file also states stride 0xEC; both need fixing.
 *
 * PS2 notes
 * - FUN_00266240 sets +0x74 |= 0x4000000 on every spawn, so that bit marks
 *   "placed by script" as distinct from a pool slot that merely exists.
 * - The pool is a fixed BSS array, not heap: allocation is a linear scan of the
 *   status array, and there is no free list.
 *
 * Unresolved callees keep their original labels.
 */

#include <stdint.h>

#define ENTITY_POOL_BASE 0x0058BEB0u
#define ENTITY_STRIDE 0x1D8u  /* 472 bytes; NOT 0xEC -- see above */
#define ENTITY_SLOT_COUNT 256
#define ENTITY_FIRST_SCRIPT_SLOT 10
#define ENTITY_SCRIPT_SLOT_COUNT 0xF6 /* 246: slots 10..255 */

extern uint8_t DAT_0058beb0[];  // pool base
extern int8_t DAT_005a96b0[];   // per-slot status: 0 free, 1 script, -1 reserved
extern int32_t iGpffffae04;     // entry count of the primary descriptor table

extern uint16_t *puGpffffb0d4;  // "current entity" the script last touched

extern void FUN_00267e78(void *entity, uint32_t byteCount);         // clear
extern void FUN_0026bfc0(uint32_t messageAddress, ...);             // debug printf
extern int16_t *FUN_00229980(int context, long typeId, long outRecord);
extern float FUN_00227070(float x, float y, void *entity);          // terrain height
extern void FUN_00229f88(void *entity, int index);
extern uint32_t FUN_00216690(float value);

/*
 * Linear free-slot scan over [start, start + count).
 * Returns the entity pointer and marks the slot 0xFF, or 0 when the pool is full.
 *
 * NOTE: Original signature: undefined2 * FUN_00265dc0(int param_1, int param_2)
 */
void *allocate_entity_slot(int start, int count)
{
  const int end = start + count;
  for (int slot = start; slot < end; ++slot)
  {
    if (DAT_005a96b0[slot] == 0)
    {
      DAT_005a96b0[slot] = -1;
      return &DAT_0058beb0[(uint32_t)slot * ENTITY_STRIDE];
    }
  }
  return 0;
}

/*
 * Allocate from the script range and initialise, unless typeId is negative.
 * This is what opcodes 0x51 and 0x65 call.
 *
 * NOTE: Original signature: long FUN_00265e28(long param_1)
 */
void *allocate_and_initialize_entity(long typeId)
{
  void *entity = allocate_entity_slot(ENTITY_FIRST_SCRIPT_SLOT, ENTITY_SCRIPT_SLOT_COUNT);
  if (entity != 0 && typeId >= 0)
  {
    FUN_00229c40(entity, typeId);
  }
  return entity;
}

/*
 * Write a world position and mirror z into the ground-height field.
 *
 * NOTE: Original signature: void FUN_002662e0(undefined4, undefined4, undefined4, int)
 */
void set_entity_position(float x, float y, float z, void *entity)
{
  uint8_t *e = (uint8_t *)entity;
  *(float *)(e + 0x20) = x;
  *(float *)(e + 0x24) = y;
  *(float *)(e + 0x28) = z;
  *(float *)(e + 0x4C) = z; // overwritten with terrain height where sampled
}

/*
 * Place a freshly spawned entity: position, terrain height, facing and flags.
 *
 * NOTE: Original signature:
 *   void FUN_00266240(undefined4 x, undefined4 y, undefined4 z, undefined4 facing,
 *                     undefined8 entity, undefined4 param_6, uint flags, undefined1 param_8)
 */
void place_spawned_entity(float x, float y, float z, uint32_t facing,
                          void *entity, uint32_t param_6, uint32_t flags, uint8_t param_8)
{
  uint8_t *e = (uint8_t *)entity;
  set_entity_position(x, y, z, entity);
  const float groundHeight = FUN_00227070(x, y, entity);

  e[0x94] = param_8;
  *(uint32_t *)(e + 0x5C) = facing;
  *(uint32_t *)(e + 0x78) = param_6;
  *(uint32_t *)(e + 0x74) = flags | 0x4000000u; // 0x4000000 = placed by script
  *(float *)(e + 0x4C) = groundHeight;
}
