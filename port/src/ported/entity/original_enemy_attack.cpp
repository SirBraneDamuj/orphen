#include "ported/entity/original_enemy_attack.h"

#include "ported/entity/original_hit_test.h"

#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    // FUN_002ebde0's four gp constants. The first is where the ring starts
    // relative to the flyer's own facing, the second is the full turn it
    // divides by the count, the third is how close to the floor the swoop has
    // to be for the dust to happen at all, and the fourth is how far above the
    // floor a puff sits.
    inline constexpr float kFGpffffaaf4_ringStartOffset = 0.523598671f; // 30 degrees
    inline constexpr float kFGpffffaaf8_fullTurn = 6.28318405f;
    inline constexpr float kFGpffffaafc_groundReach = 0.600000024f;
    inline constexpr float kFGpffffab00_puffHeight = 0.100000001f;
    inline constexpr float kFUN_002ebde0_puffRadius = 0.5f;
    inline constexpr std::int32_t kFUN_002ebde0_ringSpan = 0x3C; // split by the count

    // FUN_002ebd20 / FUN_002ebc30, type 0x10F.
    inline constexpr float kFUN_002ebc30_puffScaleEnd = 2.0f;
    inline constexpr float kFUN_002ebc30_puffScaleSpan = 1.5f;
    inline constexpr float kFUN_002ebc30_puffSpeed = 40.0f;

    // FUN_002ebad8, type 0x10E. The shot leaves bone 11 at a fixed offset and
    // travels at 60 units per 32000 ticks; the drop is whatever it takes to
    // arrive at the target's midriff over that flight.
    inline constexpr float kUGpffffaaf0_shotBoneOffset = -0.150000006f;
    inline constexpr std::size_t kFUN_002ebad8_shotBone = 0x0B;
    inline constexpr float kFUN_002ebad8_shotSpeed = 60.0f; // 0x42700000
    inline constexpr std::uint16_t kFUN_002ebad8_shotLife = 0x0C80;

    // FUN_002ec920, type 0x112: the seed lands 0.7 in front of the target,
    // along the target's own facing, and its Bezier apex is two above the
    // higher of the two ends.
    inline constexpr float kFGpffffab1c_seedLandingReach = 0.699999988f;
    inline constexpr float kFUN_002ec920_seedApex = 2.0f;
    inline constexpr std::uint16_t kFUN_002ec920_seedLife = 0x0C80;
    inline constexpr float kFUN_002ec750_seedSinkRate = 10.0f;

    // FUN_002ecc68, type 0x113: eight spores off bone 13, a random start angle
    // in whole forty-degree steps and forty-five degrees between them.
    inline constexpr float kUGpffffab20_sporeBoneOffset = -0.200000003f;
    inline constexpr std::size_t kFUN_002ecc68_sporeBone = 0x0D;
    inline constexpr float kFGpffffab24_fullTurn = 6.28318405f;
    inline constexpr float kFGpffffab28_sporeStep = 0.785398006f; // 45 degrees
    inline constexpr float kFUN_002ecc68_sporeRadius = 0.5f;
    inline constexpr float kFUN_002ecc68_sporeSpeed = 20.0f; // 0x41A00000
    inline constexpr std::int32_t kFUN_002ecc68_sporeCount = 8;
    inline constexpr float kFUN_002ecb08_sporeScaleEnd = 5.0f;
    inline constexpr float kFUN_002ecb08_sporeScaleSpan = 4.0f;

    // +0x0C's "ran into something" mask. 0x4066 is what the shot and the seed
    // both die on; the spores use the wider 0x40E6, which adds the two bits a
    // ground contact raises.
    inline constexpr std::uint32_t kCollisionStopMask = 0x4066u;
    inline constexpr std::uint32_t kFUN_002ecb08_stopMask = 0x40E6u;

    inline constexpr float kTicksPerUnit = 32000.0f;

    // FUN_0023a4b8, the bearing from one entity to another. A local copy for
    // the same reason the enemy file has one: it is three lines and belongs to
    // neither module.
    float FUN_0023a4b8_bearing(const OriginalEntity &from, const OriginalEntity &to)
    {
      return std::atan2(to.positionZ24 - from.positionZ24, to.positionX20 - from.positionX20);
    }

    std::uint32_t roll(const ActorEnvironment &environment)
    {
      return environment.random ? environment.random() : 0u;
    }

    // The body-sized box FUN_00280698 and FUN_002ef510 both build, word for
    // word: half the collision radius scaled by +0x150 in the horizontal plane,
    // and from the feet to +0x58 scaled by +0x14C.
    std::array<float, 6> body_box(const OriginalEntity &entity)
    {
      const float half = entity.radius54 * 0.5f * entity.scaleZ150;
      return {{entity.positionX20 - half, entity.positionX20 + half,
               entity.positionZ24 - half, entity.positionZ24 + half,
               entity.positionY28, entity.positionY28 + entity.height58 * entity.scale14c}};
    }
  } // namespace

  EnemyAttackRecords &DAT_005739b0_enemy80Attacks()
  {
    static EnemyAttackRecords records;
    return records;
  }

  EnemyAttackRecords &DAT_0058b140_enemy8aAttacks()
  {
    static EnemyAttackRecords records;
    return records;
  }

  void FUN_00216078_fill_attack_records(std::int16_t typeId,
                                        EnemyAttackRecords &records,
                                        const ActorEnvironment &environment)
  {
    if (environment.DAT_00354d6c_hitParameters == nullptr)
    {
      return;
    }
    // The original calls FUN_00216078 three times with indices 0, 1 and 2 and
    // ignores the return; a type with no entry leaves the destination as it
    // was, which for a global that starts zeroed is a zero record.
    for (std::uint32_t index = 0; index < records.record.size(); ++index)
    {
      const auto parameters =
          environment.DAT_00354d6c_hitParameters->FUN_00216078_record(typeId, index);
      if (parameters.has_value())
      {
        records.record[index] = *parameters;
        records.filled = true;
      }
    }
  }

  std::int8_t FUN_00280698_swoop_hit_test(OriginalEntity &entity,
                                          std::size_t slot,
                                          const ActorEnvironment &environment)
  {
    if (environment.hitTest == nullptr)
    {
      return 0;
    }
    return FUN_00215ac8_box_hit_test(entity, slot, body_box(entity),
                                     DAT_005739b0_enemy80Attacks().record[0],
                                     *environment.hitTest);
  }

  std::int8_t FUN_002ef510_effect_hit_test(
      OriginalEntity &entity,
      std::size_t slot,
      const orphen::ported::resource::HitParameters &parameters,
      const ActorEnvironment &environment)
  {
    if (environment.hitTest == nullptr)
    {
      return 0;
    }
    return FUN_00215ac8_box_hit_test(entity, slot, body_box(entity), parameters,
                                     *environment.hitTest);
  }

  void FUN_00216128_direct_hit(OriginalEntity &attacker,
                               std::size_t attackerSlot,
                               OriginalEntity &victim,
                               const orphen::ported::resource::HitParameters &parameters,
                               const ActorEnvironment &environment)
  {
    if (environment.hitTest == nullptr)
    {
      return;
    }
    // FUN_00216128 passes a null scratch, which makes FUN_00216140 use its own
    // stack frame -- nobody reads the contact count back. A local one is the
    // same thing.
    HitScratch scratch;
    FUN_00216140_apply_hit(attacker, parameters, victim, attackerSlot, *environment.hitTest,
                           scratch);
  }

  // ------------------------------------------------- the flyer's two attacks

  void FUN_002ebde0_spawn_swoop_ring(const OriginalEntity &entity,
                                     std::size_t slot,
                                     std::int32_t count,
                                     const ActorEnvironment &environment)
  {
    if (count <= 0 || environment.entityPool == nullptr || environment.descriptors == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;

    float angle = entity.facingRadians5c - kFGpffffaaf4_ringStartOffset;
    const float step =
        (static_cast<float>(kFUN_002ebde0_ringSpan / count) * kFGpffffaaf8_fullTurn) / 360.0f;

    // FUN_00227798, the single-point ground query. Without it the port cannot
    // tell how high the swoop is, and the original refuses to raise dust when
    // the flyer is more than 0.6 above the floor.
    if (!environment.FUN_00227798_probe)
    {
      return;
    }
    const float ground =
        environment.FUN_00227798_probe(entity.positionX20, entity.positionZ24, entity.positionY28)
            .height;
    if (entity.positionY28 - (ground + kFGpffffaafc_groundReach) > 0.0f)
    {
      return;
    }

    for (std::int32_t remaining = count; remaining != 0; --remaining)
    {
      // FUN_002ebd20: one puff, type 0x10F.
      const std::size_t puffSlot =
          pool.FUN_00265e28_allocate_and_initialize(kSwoopPuffTypeId, *environment.descriptors);
      if (puffSlot < kEntitySlotCount)
      {
        OriginalEntity &puff = pool.slot(puffSlot);
        puff.animationA0 = 0;
        puff.groundHeight4c = entity.groundHeight4c;
        puff.facingRadians5c = angle;
        puff.previousGroundHeight50 = entity.previousGroundHeight50;
        puff.scaleZ150 = kFUN_002ebde0_puffRadius;
        puff.scale14c = kFUN_002ebde0_puffRadius;
        puff.positionX20 = entity.positionX20 + std::cos(angle) * kFUN_002ebde0_puffRadius;
        puff.positionZ24 = entity.positionZ24 + std::sin(angle) * kFUN_002ebde0_puffRadius;
        puff.positionY28 = ground + kFGpffffab00_puffHeight;

        const auto life =
            static_cast<std::uint16_t>((static_cast<std::int32_t>(roll(environment)) % 0x1E) << 5);
        puff.puffLife198 = life;
        puff.fadeRamp62 = life;
      }
      angle += step;
    }
    (void)slot;
  }

  void FUN_002ebc30_swoop_puff(OriginalEntity &entity,
                               std::size_t slot,
                               const ActorEnvironment &environment)
  {
    // The scale ramps from 0.5 out to 2.0 across the puff's whole life. A puff
    // that rolled a zero lifetime divides by zero here in the original and is
    // freed on the same frame, before anything draws it; the port answers zero
    // for that ratio rather than carrying a NaN through the pool.
    const auto life = static_cast<std::int16_t>(entity.puffLife198);
    const auto timer = static_cast<std::int16_t>(entity.fadeRamp62);
    const float ratio = life != 0 ? static_cast<float>(timer) / static_cast<float>(life) : 0.0f;
    const float scale = kFUN_002ebc30_puffScaleEnd - ratio * kFUN_002ebc30_puffScaleSpan;
    entity.scaleZ150 = scale;
    entity.scale14c = scale;

    entity.fadeRamp62 =
        static_cast<std::uint16_t>(FUN_0023a678_countdown(timer, environment.frameTicks));
    if (entity.fadeRamp62 == 0)
    {
      if (environment.entityPool != nullptr)
      {
        environment.entityPool->releaseSlot(slot);
      }
      return;
    }

    const float travel =
        (static_cast<float>(environment.frameTicks) * kFUN_002ebc30_puffSpeed) / kTicksPerUnit;
    entity.desiredDeltaX30 += travel * std::cos(entity.facingRadians5c);
    entity.desiredDeltaZ34 += travel * std::sin(entity.facingRadians5c);
  }

  void FUN_002ebad8_spawn_shot(const OriginalEntity &entity,
                               std::size_t slot,
                               const OriginalEntity &target,
                               const orphen::ported::resource::HitParameters &parameters,
                               const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr || environment.descriptors == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    const std::size_t shotSlot =
        pool.FUN_00265e28_allocate_and_initialize(kEnemyShotTypeId, *environment.descriptors);
    if (shotSlot >= kEntitySlotCount)
    {
      return;
    }

    OriginalEntity &shot = pool.slot(shotSlot);
    shot.groundHeight4c = entity.groundHeight4c;
    shot.previousGroundHeight50 = entity.previousGroundHeight50;
    shot.scale14c = 1.0f;
    shot.facingRadians5c = entity.facingRadians5c;
    shot.scaleZ150 = 1.0f;

    orphen::ported::psm2::Vec3 muzzle{entity.positionX20, entity.positionZ24, entity.positionY28};
    if (environment.FUN_0020dc88_bone_point)
    {
      muzzle = environment.FUN_0020dc88_bone_point(
          slot, kFUN_002ebad8_shotBone,
          orphen::ported::psm2::Vec3{0.0f, kUGpffffaaf0_shotBoneOffset, 0.0f});
    }
    shot.animationA0 = 0;
    shot.positionX20 = muzzle.x;
    shot.attackPower12c = entity.attackPower12c;
    shot.positionZ24 = muzzle.y;
    shot.fadeRamp62 = kFUN_002ebad8_shotLife;
    shot.positionY28 = muzzle.z;
    shot.shotSpeed1a0 = kFUN_002ebad8_shotSpeed;

    // FUN_0023a4e8: the horizontal distance to the target. Divided by the
    // shot's speed it is the flight time, and the drop is the height it has to
    // lose over that time to arrive at the target's midriff.
    const float dx = target.positionX20 - entity.positionX20;
    const float dz = target.positionZ24 - entity.positionZ24;
    const float distance = std::sqrt(dx * dx + dz * dz);
    const float flight = distance / shot.shotSpeed1a0;
    const float drop = entity.positionY28 - (target.positionY28 + target.height58 * 0.5f);
    shot.rotationX154 = std::atan2(drop, distance);
    shot.hitParameters198 = parameters.packed();
    shot.shotDrop19c = flight != 0.0f ? drop / flight : 0.0f;
  }

  void FUN_002eb990_enemy_shot(OriginalEntity &entity,
                               std::size_t slot,
                               const ActorEnvironment &environment)
  {
    entity.fadeRamp62 = static_cast<std::uint16_t>(FUN_0023a678_countdown(
        static_cast<std::int16_t>(entity.fadeRamp62), environment.frameTicks));
    const auto release = [&]()
    {
      if (environment.entityPool != nullptr)
      {
        environment.entityPool->releaseSlot(slot);
      }
    };
    if (entity.fadeRamp62 == 0)
    {
      release();
      return;
    }

    const auto parameters =
        orphen::ported::resource::HitParameters::unpack(entity.hitParameters198);
    if (FUN_002ef510_effect_hit_test(entity, slot, parameters, environment) != 0)
    {
      release();
      return;
    }
    if ((entity.collisionFlags0c & kCollisionStopMask) != 0)
    {
      release();
      return;
    }

    const float travel =
        (entity.shotSpeed1a0 * static_cast<float>(environment.frameTicks)) / kTicksPerUnit;
    entity.desiredDeltaX30 += travel * std::cos(entity.facingRadians5c);
    entity.desiredDeltaZ34 += travel * std::sin(entity.facingRadians5c);
    entity.desiredDeltaY38 -=
        (entity.shotDrop19c * static_cast<float>(environment.frameTicks)) / kTicksPerUnit;
  }

  // ---------------------------------------------- the Maneater's two attacks

  std::int32_t FUN_002ec920_spawn_seed(const OriginalEntity &entity,
                                       std::size_t slot,
                                       const OriginalEntity &target,
                                       std::size_t bone,
                                       const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr || environment.descriptors == nullptr)
    {
      return -1;
    }
    EntityPool &pool = *environment.entityPool;
    const std::size_t seedSlot =
        pool.FUN_00265e28_allocate_and_initialize(kManeaterSeedTypeId, *environment.descriptors);
    if (seedSlot >= kEntitySlotCount)
    {
      return -1;
    }

    OriginalEntity &seed = pool.slot(seedSlot);
    seed.groundHeight4c = entity.groundHeight4c;
    seed.animationA0 = 0;
    seed.previousGroundHeight50 = entity.previousGroundHeight50;
    seed.scale14c = 1.0f;
    seed.scaleZ150 = 1.0f;
    seed.seedProgress1bc = 0.0f;
    seed.fadeRamp62 = kFUN_002ec920_seedLife;

    orphen::ported::psm2::Vec3 mouth{entity.positionX20, entity.positionZ24, entity.positionY28};
    if (environment.FUN_0020dc88_bone_point)
    {
      mouth = environment.FUN_0020dc88_bone_point(slot, bone,
                                                  orphen::ported::psm2::Vec3{0.0f, 0.0f, 0.0f});
    }
    seed.positionX20 = mouth.x;
    seed.positionZ24 = mouth.y;
    seed.positionY28 = mouth.z;
    seed.seedArcX198[0] = mouth.x;
    seed.seedArcZ1a4[0] = mouth.y;
    seed.seedArcY1b0[0] = mouth.z;

    // The landing point is 0.7 in front of the *target*, along the target's own
    // facing -- so the seed is thrown where the target is walking, not where it
    // stands.
    seed.seedArcX198[2] =
        target.positionX20 + std::cos(target.facingRadians5c) * kFGpffffab1c_seedLandingReach;
    seed.seedArcZ1a4[2] =
        target.positionZ24 + std::sin(target.facingRadians5c) * kFGpffffab1c_seedLandingReach;
    seed.seedArcY1b0[2] = target.positionY28;

    const float dx = seed.seedArcX198[2] - seed.seedArcX198[0];
    const float dz = seed.seedArcZ1a4[2] - seed.seedArcZ1a4[0];
    const float bearing = std::atan2(dz, dx);
    const float distance = std::sqrt(dx * dx + dz * dz);
    seed.seedArcX198[1] = seed.seedArcX198[0] + distance * 0.5f * std::cos(bearing);
    seed.seedArcZ1a4[1] = seed.seedArcZ1a4[0] + distance * 0.5f * std::sin(bearing);
    seed.seedArcY1b0[1] =
        (seed.seedArcY1b0[0] <= seed.seedArcY1b0[2] ? seed.seedArcY1b0[2] : seed.seedArcY1b0[0]) +
        kFUN_002ec920_seedApex;
    seed.facingRadians5c = bearing;
    return static_cast<std::int32_t>(seedSlot);
  }

  std::int32_t FUN_002ec750_seed_flight(OriginalEntity &seed,
                                        std::size_t seedSlot,
                                        const ActorEnvironment &environment)
  {
    if (seed.animationA0 == 0)
    {
      // Touching the floor ends the flight early, whatever the Bezier still
      // had left.
      if ((seed.collisionFlags0c & 1u) != 0)
      {
        seed.animationA0 = 1;
        return 0;
      }
      // Anything else it ran into instead clears +0x04 bit 3, and the next
      // frame's test below leaves the seed sitting where it stopped.
      if ((seed.collisionFlags0c & kCollisionStopMask) != 0)
      {
        seed.halfword04 = static_cast<std::uint16_t>(seed.halfword04 & 0xFFF7u);
        return 0;
      }
      if ((seed.halfword04 & 8u) == 0)
      {
        return 0;
      }

      const float total = static_cast<float>(static_cast<std::int16_t>(seed.fadeRamp62));
      const float t = total != 0.0f ? seed.seedProgress1bc / total : 1.0f;
      if (t < 1.0f)
      {
        seed.desiredDeltaX30 += FUN_0023a990_bezier(t, seed.seedArcX198) - seed.positionX20;
        seed.desiredDeltaZ34 += FUN_0023a990_bezier(t, seed.seedArcZ1a4) - seed.positionZ24;
        seed.desiredDeltaY38 += FUN_0023a990_bezier(t, seed.seedArcY1b0) - seed.positionY28;
      }
      else
      {
        seed.animationA0 = 1;
      }
      seed.seedProgress1bc += static_cast<float>(environment.frameTicks);
      return 0;
    }

    if (seed.animationA0 == 1)
    {
      // The landing clip coming round is the cue to grow the clone. The seed
      // then spends 0x0C80 ticks sinking into the ground.
      if ((seed.flags06 & 1u) != 0)
      {
        seed.animationA0 = 2;
        seed.fadeRamp62 = kFUN_002ec920_seedLife;
        return 1;
      }
      return 0;
    }

    if (seed.animationA0 != 2)
    {
      return 0;
    }

    seed.fadeRamp62 = static_cast<std::uint16_t>(FUN_0023a678_countdown(
        static_cast<std::int16_t>(seed.fadeRamp62), environment.frameTicks));
    if (seed.fadeRamp62 == 0)
    {
      if (environment.entityPool != nullptr)
      {
        environment.entityPool->releaseSlot(seedSlot);
      }
      return -1;
    }
    const float sink = seed.positionY28 - (static_cast<float>(environment.frameTicks) *
                                           kFUN_002ec750_seedSinkRate) /
                                              kTicksPerUnit;
    seed.groundHeight4c = sink;
    seed.positionY28 = sink;
    return 0;
  }

  void FUN_0028b740_grow_clone(OriginalEntity &parent,
                               std::size_t parentSlot,
                               const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr || environment.descriptors == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    const std::int32_t seedSlot = parent.enemyAttackLink1a8;
    if (seedSlot < 0 || static_cast<std::size_t>(seedSlot) >= pool.slotCount())
    {
      return;
    }

    const std::size_t targetSlot = static_cast<std::size_t>(
        parent.enemyTargetSlot1a0 < 0 ? 0 : parent.enemyTargetSlot1a0);
    const std::size_t cloneSlot = pool.FUN_00265e28_allocate_and_initialize(
        parent.typeId00, *environment.descriptors);
    if (cloneSlot >= kEntitySlotCount)
    {
      return;
    }

    const OriginalEntity &seed = pool.slot(static_cast<std::size_t>(seedSlot));
    const float bearing = FUN_0023a4b8_bearing(seed, pool.slot(targetSlot));

    // FUN_00266240(x, y, z, facing, clone, 0, 0, 0): stand the clone where the
    // seed landed, facing the target, and settle it on the floor.
    OriginalEntity &clone = pool.slot(cloneSlot);
    clone.positionX20 = seed.positionX20;
    clone.positionZ24 = seed.positionZ24;
    clone.positionY28 = seed.positionY28;
    clone.spawnParam94 = 0;
    clone.facingRadians5c = bearing;
    clone.requiredTerrainMask78 = 0;
    clone.rejectTerrainMask74 = 0x04000000u;
    if (environment.terrainSurface)
    {
      const auto surface = environment.terrainSurface(
          clone.positionX20, clone.positionZ24, clone.positionY28, clone.height58, clone.radius54,
          clone.halfword04, clone.rejectTerrainMask74);
      if (surface.has_value())
      {
        clone.groundHeight4c = surface->height;
      }
    }

    // Embedded in something solid, and the clone never happens.
    if ((clone.collisionFlags0c & 0x100u) != 0)
    {
      pool.releaseSlot(cloneSlot);
      return;
    }

    parent.enemyAttackLink1a4 = static_cast<std::int32_t>(cloneSlot);
    clone.enemyAttackLink1a4 = static_cast<std::int32_t>(parentSlot);
    clone.enemySpawnFlag1ac = 2;
    clone.staggerTimer12a = 1;
    clone.maxHitPoints128 = 1;
    clone.enemyTargetSlot1a0 = static_cast<std::int32_t>(targetSlot);
    clone.attackPower12c = parent.attackPower12c;
    FUN_00225bf0_set_state_and_animation(
        clone, 3, static_cast<std::uint16_t>((roll(environment) & 1u) == 0 ? 11 : 10));
  }

  void FUN_002ecc68_spawn_spores(const OriginalEntity &entity,
                                 std::size_t slot,
                                 const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr || environment.descriptors == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;

    orphen::ported::psm2::Vec3 mouth{entity.positionX20, entity.positionZ24, entity.positionY28};
    if (environment.FUN_0020dc88_bone_point)
    {
      mouth = environment.FUN_0020dc88_bone_point(
          slot, kFUN_002ecc68_sporeBone,
          orphen::ported::psm2::Vec3{0.0f, kUGpffffab20_sporeBoneOffset, 0.0f});
    }

    // The start angle is a whole number of forty-degree steps, up to 3560 --
    // degrees, converted the same way the ring is. It is not reduced first, so
    // nine of the ninety rolls are distinguishable and the rest repeat.
    const auto degrees =
        static_cast<float>((static_cast<std::int32_t>(roll(environment)) % 0x5A) * 0x28);
    float angle = (degrees * kFGpffffab24_fullTurn) / 360.0f;

    for (std::int32_t index = 0; index < kFUN_002ecc68_sporeCount; ++index)
    {
      const std::size_t sporeSlot =
          pool.FUN_00265e28_allocate_and_initialize(kSporeTypeId, *environment.descriptors);
      if (sporeSlot >= kEntitySlotCount)
      {
        return;
      }
      OriginalEntity &spore = pool.slot(sporeSlot);
      spore.groundHeight4c = entity.groundHeight4c;
      spore.facingRadians5c = angle;
      spore.scaleZ150 = 1.0f;
      spore.scale14c = 1.0f;
      spore.previousGroundHeight50 = entity.previousGroundHeight50;
      spore.positionX20 = mouth.x + std::cos(angle) * kFUN_002ecc68_sporeRadius;
      spore.animationA0 = 1;
      spore.positionZ24 = mouth.y + std::sin(angle) * kFUN_002ecc68_sporeRadius;
      spore.sporeSpeed198 = kFUN_002ecc68_sporeSpeed;
      spore.positionY28 = mouth.z;
      spore.sporeRise19c =
          static_cast<float>(static_cast<std::int32_t>(roll(environment)) % 7 + 3);
      angle += kFGpffffab28_sporeStep;
      const auto life = static_cast<std::int16_t>(
          ((static_cast<std::int32_t>(roll(environment)) % 0x14) << 6) + 0x0A00);
      spore.fadeRamp62 = static_cast<std::uint16_t>(life);
      spore.sporeLife1a4 = life;
    }
  }

  void FUN_002ecb08_spore(OriginalEntity &entity,
                          std::size_t slot,
                          const ActorEnvironment &environment)
  {
    const auto timer = static_cast<std::int16_t>(FUN_0023a678_countdown(
        static_cast<std::int16_t>(entity.fadeRamp62), environment.frameTicks));
    entity.fadeRamp62 = static_cast<std::uint16_t>(timer);

    if (entity.animationA0 == 1)
    {
      // The spore swells from 1.0 to 5.0 as its life runs out.
      const float ratio = entity.sporeLife1a4 != 0
                              ? static_cast<float>(timer) / static_cast<float>(entity.sporeLife1a4)
                              : 0.0f;
      const float scale = kFUN_002ecb08_sporeScaleEnd - ratio * kFUN_002ecb08_sporeScaleSpan;
      entity.scale14c = scale;
      entity.scaleZ150 = scale;

      if (timer == 0 || (entity.collisionFlags0c & kFUN_002ecb08_stopMask) != 0)
      {
        entity.animationA0 = 2;
        return;
      }
      const float travel =
          (entity.sporeSpeed198 * static_cast<float>(environment.frameTicks)) / kTicksPerUnit;
      entity.desiredDeltaX30 += travel * std::cos(entity.facingRadians5c);
      entity.desiredDeltaZ34 += travel * std::sin(entity.facingRadians5c);
      entity.desiredDeltaY38 +=
          (entity.sporeRise19c * static_cast<float>(environment.frameTicks)) / kTicksPerUnit;
      return;
    }

    if (entity.animationA0 == 2 && (entity.flags06 & 1u) != 0)
    {
      if (environment.entityPool != nullptr)
      {
        environment.entityPool->releaseSlot(slot);
      }
    }
  }

} // namespace orphen::ported::entity
