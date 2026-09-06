#pragma once

// What the two battle enemies actually throw.
//
// Both wrappers dispatch a state table full of attack states, and every one of
// those states ends in a call that leaves the enemy: a hit volume, a
// projectile, a seed. None of them were ported, which is why the animations
// played and nothing happened -- the flyer's shot fired an empty gun, the
// Maneater spat nothing, and its seed grew nothing.
//
//   src/FUN_00280698.c  0x00280698  the swoop's own box, tested every frame
//   src/FUN_002ef510.c  0x002ef510  the same box, with the record as an argument
//   src/FUN_002ebde0.c  0x002ebde0  six dust puffs in an arc under the swoop
//   src/FUN_002ebd20.c  0x002ebd20  one of them, type 0x10F
//   src/FUN_002ebc30.c  0x002ebc30  type 0x10F's behaviour
//   src/FUN_002ebad8.c  0x002ebad8  the flyer's shot, type 0x10E
//   src/FUN_002eb990.c  0x002eb990  type 0x10E's behaviour
//   src/FUN_002ec920.c  0x002ec920  the Maneater's seed, type 0x112
//   src/FUN_002ec750.c  0x002ec750  type 0x112's arc, ticked by its parent
//   src/FUN_0028b740.c  0x0028b740  the seed landing and growing a clone
//   src/FUN_002ecc68.c  0x002ecc68  eight poison spores, type 0x113
//   src/FUN_002ecb08.c  0x002ecb08  type 0x113's behaviour
//   src/FUN_00216128.c  0x00216128  FUN_00216140 with no scratch: a direct hit
//
// **Only two of the six attacks test anything.** The flyer's swoop sweeps its
// own body box through FUN_00215ac8 on every frame of the dive, and its shot
// sweeps the projectile's box the same way. The other four -- the Maneater's
// bite, its spit, its clone's approach and the clone's grab -- charge the
// victim through FUN_00216128 with no test at all: the state has already
// decided the hit lands, and the entity it names is the one that pays. The
// eight spore orbs are decoration; FUN_002ecb08 has no hit test in it.
//
// **Type 0x112 has no actor handler.** Its slot in PTR_LAB_0031CAB0 is
// FUN_00239e78, the no-op, so the seed does not tick itself -- the Maneater
// that spat it calls FUN_002ec750 on it from its own wrapper and reads the
// return: 1 means the seed just landed and it is time to grow the clone, -1
// means the seed freed itself. That is also why a Maneater standing down has
// to tear the link down by hand.

#include "ported/entity/actor_frame_update.h"
#include "ported/entity/original_entity.h"
#include "ported/resource/hit_parameter_table.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace orphen::ported::entity
{

  // The four effect types, by the ids FUN_00265e28 is handed.
  inline constexpr std::int32_t kEnemyShotTypeId = 0x10E;    // FUN_002eb990
  inline constexpr std::int32_t kSwoopPuffTypeId = 0x10F;    // FUN_002ebc30
  inline constexpr std::int32_t kManeaterSeedTypeId = 0x112; // no handler
  inline constexpr std::int32_t kSporeTypeId = 0x113;        // FUN_002ecb08

  inline constexpr std::uint32_t kFUN_002eb990_enemyShot = 0x002EB990;
  inline constexpr std::uint32_t kFUN_002ebc30_swoopPuff = 0x002EBC30;
  inline constexpr std::uint32_t kFUN_002ecb08_spore = 0x002ECB08;

  // DAT_005739B0 and DAT_0058B140: three four-byte attack records per enemy
  // type, filled by that type's state 0 with FUN_00216078 and read by the state
  // that fires. Globals in the original -- one bank per type, not one per
  // entity -- so globals here.
  struct EnemyAttackRecords
  {
    std::array<orphen::ported::resource::HitParameters, 3> record{};
    bool filled = false;
  };
  EnemyAttackRecords &DAT_005739b0_enemy80Attacks();
  EnemyAttackRecords &DAT_0058b140_enemy8aAttacks();

  // The three FUN_00216078 calls at the top of FUN_0027f978 / FUN_0028ae10.
  void FUN_00216078_fill_attack_records(std::int16_t typeId,
                                        EnemyAttackRecords &records,
                                        const ActorEnvironment &environment);

  // FUN_00215ac8 through a box the size of the attacker. FUN_00280698 is the
  // swoop's, with the type's record 0; FUN_002ef510 is the same box for an
  // effect entity carrying its own record. Both return the contact count.
  std::int8_t FUN_00280698_swoop_hit_test(OriginalEntity &entity,
                                          std::size_t slot,
                                          const ActorEnvironment &environment);
  std::int8_t FUN_002ef510_effect_hit_test(
      OriginalEntity &entity,
      std::size_t slot,
      const orphen::ported::resource::HitParameters &parameters,
      const ActorEnvironment &environment);

  // FUN_00216128(record, attacker, victim): FUN_00216140 with a null scratch,
  // which is the whole of "this attack simply hits".
  void FUN_00216128_direct_hit(OriginalEntity &attacker,
                               std::size_t attackerSlot,
                               OriginalEntity &victim,
                               const orphen::ported::resource::HitParameters &parameters,
                               const ActorEnvironment &environment);

  // FUN_002ebde0(entity, 6): the ring of dust the swoop kicks up, six puffs
  // spread over fifty degrees ahead of the flyer. Only when it is within 0.6 of
  // the floor.
  void FUN_002ebde0_spawn_swoop_ring(const OriginalEntity &entity,
                                     std::size_t slot,
                                     std::int32_t count,
                                     const ActorEnvironment &environment);

  // FUN_002ebad8: the flyer's shot, off bone 11, aimed down at the target.
  void FUN_002ebad8_spawn_shot(const OriginalEntity &entity,
                               std::size_t slot,
                               const OriginalEntity &target,
                               const orphen::ported::resource::HitParameters &parameters,
                               const ActorEnvironment &environment);

  // FUN_002ec920: the Maneater's seed, lobbed from bone `bone` to a point 0.7
  // in front of the target. Returns the pool slot, or -1.
  std::int32_t FUN_002ec920_spawn_seed(const OriginalEntity &entity,
                                       std::size_t slot,
                                       const OriginalEntity &target,
                                       std::size_t bone,
                                       const ActorEnvironment &environment);

  // FUN_002ec750: one frame of the seed, run by its parent. 0 = still flying,
  // 1 = it landed and the clip came round, -1 = it freed itself.
  std::int32_t FUN_002ec750_seed_flight(OriginalEntity &seed,
                                        std::size_t seedSlot,
                                        const ActorEnvironment &environment);

  // FUN_0028b740: the seed becoming a second Maneater, bound to its parent.
  void FUN_0028b740_grow_clone(OriginalEntity &parent,
                               std::size_t parentSlot,
                               const ActorEnvironment &environment);

  // FUN_002ecc68: eight poison spores off bone 13, spread evenly and rising.
  void FUN_002ecc68_spawn_spores(const OriginalEntity &entity,
                                 std::size_t slot,
                                 const ActorEnvironment &environment);

  // The behaviours the actor dispatch reaches.
  void FUN_002eb990_enemy_shot(OriginalEntity &entity,
                               std::size_t slot,
                               const ActorEnvironment &environment);
  void FUN_002ebc30_swoop_puff(OriginalEntity &entity,
                               std::size_t slot,
                               const ActorEnvironment &environment);
  void FUN_002ecb08_spore(OriginalEntity &entity,
                          std::size_t slot,
                          const ActorEnvironment &environment);

} // namespace orphen::ported::entity
