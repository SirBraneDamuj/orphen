#include "ported/entity/original_swarm_crab.h"

#include "ported/battle/battle_target_markers.h"

#include "ported/entity/actor_dispatch_table.h"
#include "ported/entity/original_bubble_effect.h"
#include "ported/entity/original_hit_test.h"

#include <array>
#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    // ------------------------------------------------------------- constants
    //
    // Everything this type reads is one of the thirteen gp words in
    // 0x00352FB8..0x00352FE4. Nine of them are the same two values -- ten
    // degrees of turn and a full turn in radians -- and they are kept apart
    // because the original keeps them apart.
    inline constexpr float kFGpffff9048_walkTurnRate = 0.174532890319824f;
    inline constexpr float kDAT_00352fbc_markTurnRate = 0.174532890319824f;
    inline constexpr float kDAT_00352fc0_fullTurn = 6.28318405151367f;
    inline constexpr float kDAT_00352fc4_millTurnRate = 0.174532890319824f;
    inline constexpr float kDAT_00352fc8_markCreep = 0.0500000007450581f;
    inline constexpr float kDAT_00352fcc_wallTurn = 6.28318405151367f;
    inline constexpr float kDAT_00352fd0_aimJitter = 6.28318405151367f;
    inline constexpr float kFGpffff9064_leapTurnRate = 0.174532890319824f;
    inline constexpr float kFGpffff9068_landPast = 0.300000011920929f;
    inline constexpr float kUGpffff906c_leapPitch = -0.785398006439209f;
    inline constexpr float kFGpffff9070_leapSweep = 6.28318405151367f;
    inline constexpr float kFGpffff9074_leapPitchBase = -0.785398006439209f;

    // The two speeds. The wander rolls 10..29 fresh for each leg; the leap and
    // the "get back to the mark" correction are both fixed.
    inline constexpr float kFUN_00276f50_wanderSpeedBase = 10.0f;
    inline constexpr std::uint32_t kFUN_00276f50_wanderSpeedSpread = 0x14;
    inline constexpr float kFUN_00277410_correctionSpeed = 20.0f;
    inline constexpr float kFUN_002778b0_leapSpeed = 30.0f;

    // FUN_00277110's mark: x in -11..-6, z 4.0 either side by up to 0.8. That
    // is the far end of the beach, the same corner FUN_0027D230's run spots use.
    inline constexpr float kFUN_00277110_markBaseX = 6.0f;
    inline constexpr std::uint32_t kFUN_00277110_markSpreadX = 6;
    inline constexpr float kFUN_00277110_markBaseZ = 4.0f;
    inline constexpr std::uint32_t kFUN_00277110_markSpreadZ = 5;

    // FUN_00277410's mill. The heading is a whole number of degrees out of 360,
    // the hold is 50..149 beats of 0x20, and the two long-odds rolls are one in
    // a hundred for the hop and one in ten of those for a bubble.
    inline constexpr std::uint32_t kFUN_00277410_headingDegrees = 0x168;
    inline constexpr std::int16_t kFUN_00277410_holdBase = 0x32;
    inline constexpr std::uint32_t kFUN_00277410_holdSpread = 100;
    inline constexpr std::uint32_t kFUN_00277410_hopOdds = 100;
    inline constexpr std::uint32_t kFUN_00277410_bubbleOdds = 10;
    inline constexpr std::uint32_t kFUN_00277410_hopHeight = 3;
    inline constexpr std::uint32_t kFUN_00277410_wallJitter = 10;
    inline constexpr std::uint32_t kFUN_00277410_aimJitter = 0x1E;
    inline constexpr float kFUN_00277410_markSlack = 0.5f;
    inline constexpr float kFUN_00277410_creepRange = 1.0f;

    // FUN_002778b0's leap. Phase 0 closes to a unit and a half of the player,
    // phase 2 walks the arc over 0xA00 ticks against a 2560 divisor -- so the
    // ratio runs to 1.0 exactly one tick before the beat ends.
    inline constexpr float kFUN_002778b0_approachGap = 1.5f;
    inline constexpr float kFUN_002778b0_arcApex = 2.5f;
    inline constexpr std::int16_t kFUN_002778b0_arcTicks = 0x0A00;
    inline constexpr float kFUN_002778b0_arcScale = 2560.0f;
    inline constexpr std::uint16_t kFUN_002778b0_leapCue = 0x11C;
    inline constexpr float kFUN_002778b0_pitchSweep = 90.0f;
    inline constexpr std::uint32_t kFUN_002778b0_glow = 0x00FA98AEu;
    inline constexpr std::uint32_t kFUN_002778b0_wallMask = 2u;

    // FUN_00276c30's own two: the flinch hold, and the height either side of
    // zero past which the wrapper simply removes the entity.
    inline constexpr std::uint16_t kFUN_00276c30_flinchTicks = 0x03C0;
    inline constexpr float kFUN_00276c30_heightLimit = 10.0f;

    // FUN_00276de0's size roll: 2.00 to 2.99, on both scale axes.
    inline constexpr float kFUN_00276de0_scaleBase = 2.0f;
    inline constexpr std::uint32_t kFUN_00276de0_scaleSpread = 100;

    // ------------------------------------------------------------- shorthands

    std::uint32_t roll(const ActorEnvironment &environment)
    {
      return environment.random ? environment.random() : 0;
    }

    float cos_of(float radians) { return std::cos(radians); }
    float sin_of(float radians) { return std::sin(radians); }

    float FUN_0023a4b8_bearing(const OriginalEntity &from, const OriginalEntity &to)
    {
      return std::atan2(to.positionZ24 - from.positionZ24, to.positionX20 - from.positionX20);
    }

    float FUN_0023a4e8_distance_between(const OriginalEntity &from, const OriginalEntity &to)
    {
      const float dx = to.positionX20 - from.positionX20;
      const float dz = to.positionZ24 - from.positionZ24;
      return std::sqrt(dx * dx + dz * dz);
    }

    std::int16_t FUN_0023a6d0_travel_ticks(float speed,
                                           const OriginalEntity &entity,
                                           float x,
                                           float z)
    {
      const float dx = x - entity.positionX20;
      const float dz = z - entity.positionZ24;
      const std::int32_t raw =
          static_cast<std::int32_t>(std::sqrt(dx * dx + dz * dz) / (speed / 1000.0f));
      return static_cast<std::int16_t>((raw << 21) >> 16);
    }

    // The same subtract-store-test-signed countdown every one of these uses.
    bool countdown(std::uint16_t &timer, std::uint32_t frameTicks)
    {
      const std::int32_t remaining =
          static_cast<std::int32_t>(timer) - static_cast<std::int32_t>(frameTicks & 0xFFFFu);
      timer = static_cast<std::uint16_t>(remaining);
      return (remaining * 0x10000) < 0;
    }

    // The walk every state shares: speed in +0x1A0, heading in +0x5C.
    void step_along_facing(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      const float travel =
          (entity.swarmSpeed1a0 *
           static_cast<float>(static_cast<std::int32_t>(environment.frameTicks))) /
          32000.0f;
      entity.desiredDeltaX30 += travel * cos_of(entity.facingRadians5c);
      entity.desiredDeltaZ34 += travel * sin_of(entity.facingRadians5c);
    }

    // -------------------------------------------------------------- the states

    // FUN_00276de0, state 0. FUN_0027F978 line for line -- the stat record, the
    // three attack records, the actor bind -- plus the one thing that is only
    // here: the size roll. A swarm crab is two to three times the model's own
    // scale, which is why a hundred of them read as a heap rather than a shoal.
    //
    // The original follows a failed FUN_0023F8B8 with FUN_0026BFC0, its fatal
    // error. It cannot fail on hardware because the crab is dead by the time any
    // of these exist and the table it is asking for a slot in has just been
    // emptied; here an unbound record is -1 and the state dispatch below simply
    // does not run the states that need one.
    void FUN_00276de0_state0_init(OriginalEntity &entity,
                                  std::size_t slot,
                                  const ActorEnvironment &environment)
    {
      if (environment.uGpffffadf8_stats != nullptr)
      {
        const auto record = environment.uGpffffadf8_stats->FUN_00229688_record(
            0, static_cast<std::int32_t>(entity.typeId00) - 0x7C);
        if (record.has_value())
        {
          entity.radius54 = record->radius0c;
          entity.hitVolumeRadius11c = record->radius0c;
          entity.height58 = record->height10;
          entity.hitVolumeHeight120 = record->height10;
          const auto hitPoints =
              static_cast<std::int16_t>(static_cast<std::int8_t>(record->byte06));
          entity.staggerTimer12a = static_cast<std::uint16_t>(hitPoints);
          entity.maxHitPoints128 = static_cast<std::uint16_t>(hitPoints);
          entity.attackPower12c = static_cast<std::uint16_t>(
              static_cast<std::int16_t>(static_cast<std::int8_t>(record->byte07)));
          entity.defence12e = static_cast<std::uint16_t>(
              static_cast<std::int16_t>(static_cast<std::int8_t>(record->byte08)));
        }
      }

      FUN_00216078_fill_attack_records(static_cast<std::int16_t>(entity.typeId00),
                                       DAT_00573778_swarmAttacks(), environment);

      entity.battleDesiredFacing19c = entity.facingRadians5c;
      entity.battleFlags96 = static_cast<std::uint8_t>(entity.battleFlags96 | 1u);
      entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 1u);

      const float scale =
          static_cast<float>(roll(environment) % kFUN_00276de0_scaleSpread) / 100.0f +
          kFUN_00276de0_scaleBase;
      entity.scale14c = scale;
      entity.scaleZ150 = scale;

      if (environment.FUN_0023f8b8_bind_battle_actor)
      {
        entity.battleActorRecord198 = environment.FUN_0023f8b8_bind_battle_actor(slot);
      }

      FUN_00225bf0_set_state_and_animation(entity, 1, 2);
    }

    // FUN_00276f50, state 1 -- walk to the mark at +0x3C/+0x40 and hand on to
    // state 2. A wall on the way (+0x0C bit 1, 5 or 6) turns it ten degrees off
    // its current facing rather than off the mark, so it slides along the wall
    // instead of grinding into it.
    void FUN_00276f50_state1_walk(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      if (entity.animationA0 == 2)
      {
        entity.battleDesiredFacing19c = std::atan2(entity.swarmMarkZ40 - entity.positionZ24,
                                                   entity.swarmMarkX3c - entity.positionX20);
        entity.swarmSpeed1a0 =
            static_cast<float>(static_cast<std::int32_t>(roll(environment) %
                                                         kFUN_00276f50_wanderSpeedSpread) +
                               static_cast<std::int32_t>(kFUN_00276f50_wanderSpeedBase));
        entity.fadeRamp62 = static_cast<std::uint16_t>(FUN_0023a6d0_travel_ticks(
            entity.swarmSpeed1a0, entity, entity.swarmMarkX3c, entity.swarmMarkZ40));
        FUN_00225bc8_set_animation(entity, 0);
      }

      const float step = FUN_0023a320_approach_angle(
          entity.facingRadians5c, entity.battleDesiredFacing19c,
          static_cast<float>(static_cast<std::int32_t>(environment.frameTicks)) *
              kFGpffff9048_walkTurnRate * 0.03125f);
      if (step != 0.0f)
      {
        entity.facingRadians5c += step;
        return;
      }
      if (countdown(entity.fadeRamp62, environment.frameTicks))
      {
        FUN_00225bf0_set_state_and_animation(entity, 2, 2);
        return;
      }
      if ((entity.collisionFlags0c & 0x62u) != 0)
      {
        entity.battleDesiredFacing19c = entity.facingRadians5c + kFGpffff9048_walkTurnRate;
      }
      step_along_facing(entity, environment);
    }

    // FUN_00277110, state 2 -- roll the next mark and walk to that. Identical to
    // state 1 once the mark is chosen; the only difference is that this one
    // chooses.
    void FUN_00277110_state2_pick_mark(OriginalEntity &entity,
                                       const ActorEnvironment &environment)
    {
      if (entity.animationA0 == 2)
      {
        entity.swarmMarkX3c = -static_cast<float>(
            static_cast<std::int32_t>(roll(environment) % kFUN_00277110_markSpreadX) +
            static_cast<std::int32_t>(kFUN_00277110_markBaseX));
        const float offset =
            static_cast<float>(
                static_cast<std::int32_t>((roll(environment) % kFUN_00277110_markSpreadZ) * 0x14u)) /
            100.0f;
        entity.swarmMarkZ40 = ((roll(environment) & 1u) == 0)
                                  ? (kFUN_00277110_markBaseZ - offset)
                                  : (kFUN_00277110_markBaseZ + offset);
        entity.battleDesiredFacing19c = std::atan2(entity.swarmMarkZ40 - entity.positionZ24,
                                                   entity.swarmMarkX3c - entity.positionX20);
        entity.swarmSpeed1a0 =
            static_cast<float>(static_cast<std::int32_t>(roll(environment) %
                                                         kFUN_00276f50_wanderSpeedSpread) +
                               static_cast<std::int32_t>(kFUN_00276f50_wanderSpeedBase));
        entity.fadeRamp62 = static_cast<std::uint16_t>(FUN_0023a6d0_travel_ticks(
            entity.swarmSpeed1a0, entity, entity.swarmMarkX3c, entity.swarmMarkZ40));
        FUN_00225bc8_set_animation(entity, 0);
      }

      const float step = FUN_0023a320_approach_angle(
          entity.facingRadians5c, entity.battleDesiredFacing19c,
          static_cast<float>(static_cast<std::int32_t>(environment.frameTicks)) *
              kDAT_00352fbc_markTurnRate * 0.03125f);
      if (step != 0.0f)
      {
        entity.facingRadians5c += step;
        return;
      }
      if (countdown(entity.fadeRamp62, environment.frameTicks))
      {
        FUN_00225bf0_set_state_and_animation(entity, 3, 2);
        return;
      }
      if ((entity.collisionFlags0c & 0x62u) != 0)
      {
        entity.battleDesiredFacing19c = entity.facingRadians5c + kDAT_00352fbc_markTurnRate;
      }
      step_along_facing(entity, environment);
    }

    // FUN_00277410, state 3 -- the mill, and the state the whole swarm sits in
    // between FUN_0027BA20's wake-ups.
    //
    // A heading is rolled in whole degrees and held 50..149 beats. Every frame
    // there is a one in a hundred chance of a small upward nudge, and one in ten
    // of those also blows a bubble. When the hold runs out the walk clip goes
    // back on and, if the player is more than a unit away, the mark creeps 0.05
    // toward the negative x end of the beach -- which is how the swarm drifts as
    // a body rather than each one wandering on its own.
    //
    // The mark itself is only consulted when the crab has strayed more than half
    // a unit from it; then the heading is re-aimed at it with up to 29 degrees of
    // jitter and the speed goes to a flat 20.
    void FUN_00277410_state3_mill(OriginalEntity &entity,
                                  std::size_t slot,
                                  const ActorEnvironment &environment)
    {
      EntityPool &pool = *environment.entityPool;

      if (entity.animationA0 == 2)
      {
        entity.battleDesiredFacing19c =
            (static_cast<float>(roll(environment) % kFUN_00277410_headingDegrees) *
             kDAT_00352fc0_fullTurn) /
            360.0f;
        entity.fadeRamp62 = static_cast<std::uint16_t>(
            (static_cast<std::int16_t>(roll(environment) % kFUN_00277410_holdSpread) +
             kFUN_00277410_holdBase) *
            0x20);
        entity.swarmSpeed1a0 =
            static_cast<float>(static_cast<std::int32_t>(roll(environment) %
                                                         kFUN_00276f50_wanderSpeedSpread) +
                               static_cast<std::int32_t>(kFUN_00276f50_wanderSpeedBase));
        FUN_00225bc8_set_animation(entity, 0);
      }

      const float step = FUN_0023a320_approach_angle(
          entity.facingRadians5c, entity.battleDesiredFacing19c,
          static_cast<float>(static_cast<std::int32_t>(environment.frameTicks)) *
              kDAT_00352fc4_millTurnRate * 0.03125f);
      if (step != 0.0f)
      {
        entity.facingRadians5c += step;
      }

      // Note the countdown runs whether or not the turn finished -- unlike
      // every other state here, this one does not gate its body on the turn.
      if (countdown(entity.fadeRamp62, environment.frameTicks))
      {
        FUN_00225bc8_set_animation(entity, 2);
        if (FUN_0023a4e8_distance_between(pool.slot(0), entity) > kFUN_00277410_creepRange)
        {
          entity.swarmMarkX3c -= kDAT_00352fc8_markCreep;
        }
        return;
      }

      if (roll(environment) % kFUN_00277410_hopOdds == 0)
      {
        entity.verticalVelocity44 =
            static_cast<float>(roll(environment) % kFUN_00277410_hopHeight) / 100.0f;
        if (roll(environment) % kFUN_00277410_bubbleOdds == 0)
        {
          FUN_002eac48_spawn_bubble(entity, 4,
                                    orphen::ported::psm2::Vec3{entity.positionX20,
                                                               entity.positionZ24,
                                                               entity.positionY28},
                                    0, nullptr, environment);
        }
      }

      if ((entity.collisionFlags0c & 0x62u) != 0)
      {
        entity.battleDesiredFacing19c =
            entity.facingRadians5c +
            (static_cast<float>(roll(environment) % kFUN_00277410_wallJitter) *
             kDAT_00352fcc_wallTurn) /
                360.0f;
      }

      const float toMarkX = entity.swarmMarkX3c - entity.positionX20;
      const float toMarkZ = entity.swarmMarkZ40 - entity.positionZ24;
      if (std::sqrt(toMarkX * toMarkX + toMarkZ * toMarkZ) > kFUN_00277410_markSlack)
      {
        const float bearing = std::atan2(toMarkZ, toMarkX);
        entity.swarmSpeed1a0 = kFUN_00277410_correctionSpeed;
        entity.battleDesiredFacing19c =
            bearing + (static_cast<float>(roll(environment) % kFUN_00277410_aimJitter) *
                       kDAT_00352fd0_aimJitter) /
                          360.0f;
      }

      step_along_facing(entity, environment);
      static_cast<void>(slot);
    }

    // FUN_00277860, state 4 -- the flinch. FUN_0023A678 floors the countdown at
    // zero rather than letting it go negative, so this ends on the exact frame
    // the hold runs out.
    void FUN_00277860_state4_flinch(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      const std::int16_t remaining = FUN_0023a678_countdown(
          static_cast<std::int16_t>(entity.fadeRamp62), environment.frameTicks);
      entity.fadeRamp62 = static_cast<std::uint16_t>(remaining);
      if (remaining == 0)
      {
        FUN_00225bf0_set_state_and_animation(entity, 2, 2);
      }
    }

    // LAB_00277828, state 5 -- the death, and the only state in the game the
    // port reaches through a bare label. Thirteen instructions: wait for the
    // clip, then raise +0x06 bit 0x10 and +0x04 bit 0x800, which hands the
    // entity to FUN_0023A568's fade path from the next frame on.
    void LAB_00277828_state5_death(OriginalEntity &entity)
    {
      if (entity.spawnParam94 != 0)
      {
        return;
      }
      if ((entity.flags06 & 1u) == 0)
      {
        return;
      }
      entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10u);
      entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 0x800u);
    }

    // FUN_00277ca0: the leap's hit box, its own body at half radius, swept
    // through FUN_00215AC8 with attack record 0 of this type's bank.
    void FUN_00277ca0_leap_hit_test(OriginalEntity &entity,
                                    std::size_t slot,
                                    const ActorEnvironment &environment)
    {
      if (environment.hitTest == nullptr || !DAT_00573778_swarmAttacks().filled)
      {
        return;
      }
      const float reach = entity.radius54 * 0.5f * entity.scaleZ150;
      const std::array<float, 6> box{{entity.positionX20 - reach, entity.positionX20 + reach,
                                      entity.positionZ24 - reach, entity.positionZ24 + reach,
                                      entity.positionY28,
                                      entity.positionY28 + entity.height58 * entity.scale14c}};
      FUN_00215ac8_box_hit_test(entity, slot, box, DAT_00573778_swarmAttacks().record[0],
                                *environment.hitTest);
    }

    // FUN_002778b0, state 6 -- the leap, three phases on +0x94.
    //
    //   0  close to a unit and a half short of the player at thirty
    //   1  cue 0x11C, drop the glow, raise +0x04 bit 3, pitch nose-down a
    //      quarter turn and build the arc: from where it stands, by way of a
    //      point 2.5 above the victim, to 0.3 past him
    //   2  walk the arc over 0xA00 ticks, sweeping the pitch a quarter turn back
    //      the other way and sweeping the body box every frame. A wall resets the
    //      timer to zero rather than ending the leap, so it keeps trying.
    void FUN_002778b0_state6_leap(OriginalEntity &entity,
                                  std::size_t slot,
                                  const ActorEnvironment &environment)
    {
      EntityPool &pool = *environment.entityPool;

      if (entity.animationA0 == 2)
      {
        FUN_00215e48_clear_hit_set(entity);
        entity.rotationX154 = 0.0f;
        entity.spawnParam94 = 0;
        FUN_00225bc8_set_animation(entity, 3);
        entity.fadeColor138 = kFUN_002778b0_glow;
      }

      const float step = FUN_0023a320_approach_angle(
          entity.facingRadians5c, entity.battleDesiredFacing19c,
          static_cast<float>(static_cast<std::int32_t>(environment.frameTicks)) *
              kFGpffff9064_leapTurnRate * 0.03125f);
      if (step != 0.0f)
      {
        entity.facingRadians5c += step;
        return;
      }

      const OriginalEntity &player = pool.slot(0);

      if (entity.spawnParam94 == 0)
      {
        if (!countdown(entity.fadeRamp62, environment.frameTicks))
        {
          return;
        }
        entity.swarmVictim1c8 = 0;
        entity.swarmSpeed1a0 = kFUN_002778b0_leapSpeed;
        entity.battleDesiredFacing19c = FUN_0023a4b8_bearing(entity, player);
        // The approach point is measured from the *player* back toward us, so
        // the crab stops a unit and a half short however it came in.
        const float back = FUN_0023a4b8_bearing(player, entity);
        const float aimX = player.positionX20 + cos_of(back) * kFUN_002778b0_approachGap;
        const float aimZ = player.positionZ24 + sin_of(back) * kFUN_002778b0_approachGap;
        entity.fadeRamp62 = static_cast<std::uint16_t>(
            FUN_0023a6d0_travel_ticks(entity.swarmSpeed1a0, entity, aimX, aimZ));
        entity.spawnParam94 = 1;
        return;
      }

      if (entity.spawnParam94 == 1)
      {
        if (!countdown(entity.fadeRamp62, environment.frameTicks))
        {
          step_along_facing(entity, environment);
          return;
        }
        entity.fadeColor138 = 0;
        if (environment.FUN_00267d38_playSound)
        {
          environment.FUN_00267d38_playSound(kFUN_002778b0_leapCue, entity);
        }
        const std::size_t victim = static_cast<std::size_t>(
            entity.swarmVictim1c8 >= 0 && static_cast<std::size_t>(entity.swarmVictim1c8) <
                                              pool.slotCount()
                ? entity.swarmVictim1c8
                : 0);
        const OriginalEntity &target = pool.slot(victim);

        entity.fadeRamp62 = 0;
        entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 8u);
        entity.rotationX154 = kUGpffff906c_leapPitch;
        entity.spawnParam94 = 2;

        const float bearing = FUN_0023a4b8_bearing(entity, target);
        entity.facingRadians5c = bearing;
        entity.swarmArcX1a4[0] = entity.positionX20;
        entity.swarmArcZ1b0[0] = entity.positionZ24;
        entity.swarmArcY1bc[0] = entity.positionY28;
        entity.swarmArcX1a4[2] = target.positionX20 + cos_of(bearing) * kFGpffff9068_landPast;
        entity.swarmArcZ1b0[2] = target.positionZ24 + sin_of(bearing) * kFGpffff9068_landPast;
        entity.swarmArcX1a4[1] = entity.positionX20;
        entity.swarmArcZ1b0[1] = entity.positionZ24;
        entity.swarmArcY1bc[2] = target.positionY28;
        entity.swarmArcY1bc[1] = target.positionY28 + kFUN_002778b0_arcApex;
        return;
      }

      if (entity.spawnParam94 != 2)
      {
        return;
      }

      const auto elapsed = static_cast<std::int16_t>(
          static_cast<std::int16_t>(entity.fadeRamp62) +
          static_cast<std::int16_t>(environment.frameTicks & 0xFFFFu));
      entity.fadeRamp62 = static_cast<std::uint16_t>(elapsed);
      if (elapsed > kFUN_002778b0_arcTicks)
      {
        entity.rotationX154 = 0.0f;
        entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 & 0xFFF7u);
        FUN_00225bf0_set_state_and_animation(entity, 3, 2);
        return;
      }
      if ((entity.collisionFlags0c & kFUN_002778b0_wallMask) != 0)
      {
        entity.fadeRamp62 = 0;
        return;
      }

      FUN_00277ca0_leap_hit_test(entity, slot, environment);
      const float t = static_cast<float>(elapsed) / kFUN_002778b0_arcScale;
      entity.desiredDeltaX30 += FUN_0023a990_bezier(t, entity.swarmArcX1a4) - entity.positionX20;
      entity.desiredDeltaZ34 += FUN_0023a990_bezier(t, entity.swarmArcZ1b0) - entity.positionZ24;
      entity.desiredDeltaY38 += FUN_0023a990_bezier(t, entity.swarmArcY1bc) - entity.positionY28;
      entity.rotationX154 =
          (t * kFUN_002778b0_pitchSweep * kFGpffff9070_leapSweep) / 360.0f +
          kFGpffff9074_leapPitchBase;
    }

    // FUN_00276d50, the action check. Two of the battle module's orders mean
    // something to a swarm crab and the rest are swallowed: 10 says "stop, and
    // stay stopped" -- it is the only one that returns true, and the only one
    // whose pending byte is left standing so it fires again next frame -- and 11
    // says "hold" without suppressing the state.
    bool FUN_00276d50_action_check(OriginalEntity &entity,
                                   ActorEnvironment::BattleActorView &view,
                                   bool haveRecord)
    {
      if (!haveRecord)
      {
        return false;
      }
      if (view.pendingAction0e == 0)
      {
        return false;
      }
      entity.rotationX154 = 0.0f;
      view.flags38 = 1;
      entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 & 0xFFF7u);

      if (view.pendingAction0e == 10)
      {
        view.currentAction0f = 10;
        return true;
      }
      if (view.pendingAction0e == 0x0B)
      {
        view.currentAction0f = 0x0B;
        return false;
      }
      view.pendingAction0e = 0;
      return false;
    }

    // FUN_00248040: drop the entity's "I am a battle participant" bit and, if
    // one of the twenty DAT_003253C0 rows is aiming at it, take that row and its
    // 0x192 cursor down with it. A swarm crab walking off the arena is the
    // commonest way the player's target disappears, so the second half matters
    // as much as the first.
    void FUN_00248040_release_record(OriginalEntity &entity,
                                     std::size_t slot,
                                     const ActorEnvironment &environment)
    {
      entity.battleFlags96 = static_cast<std::uint8_t>(entity.battleFlags96 & 0xFEu);
      if (environment.DAT_003253c0_markers != nullptr && environment.entityPool != nullptr)
      {
        environment.DAT_003253c0_markers->FUN_00248040_unmark(*environment.entityPool,
                                                              static_cast<std::int32_t>(slot));
      }
    }

  } // namespace

  EnemyAttackRecords &DAT_00573778_swarmAttacks()
  {
    static EnemyAttackRecords records{};
    return records;
  }

  void FUN_00276c30_swarm_crab(OriginalEntity &entity,
                               std::size_t slot,
                               const ActorEnvironment &environment,
                               ActorTrace &trace)
  {
    if (environment.entityPool == nullptr || environment.dispatchTable == nullptr)
    {
      return;
    }

    ActorEnvironment::BattleActorView view;
    const bool haveRecord = static_cast<bool>(environment.DAT_00354eb4_battleActor) &&
                            environment.DAT_00354eb4_battleActor(entity.battleActorRecord198, view);
    const auto publish = [&]()
    {
      if (haveRecord && environment.DAT_00354eb4_setBattleActor)
      {
        environment.DAT_00354eb4_setBattleActor(entity.battleActorRecord198, view);
      }
    };

    if (!FUN_00276d50_action_check(entity, view, haveRecord))
    {
      // FUN_0023a068 inlined, and note it returns from the *whole* wrapper: a
      // frozen swarm crab does not run its state at all, where the crab boss's
      // wrapper only skips its action check.
      const std::int8_t freeze = entity.freezeTimerBd;
      if (freeze != 0)
      {
        entity.freezeTimerBd = static_cast<std::int8_t>(freeze - 1);
        if (freeze != 1)
        {
          publish();
          return;
        }
      }

      if (entity.pendingDamageBe != 0)
      {
        const std::uint16_t cleared =
            static_cast<std::uint16_t>(entity.halfword04 & 0xFFF7u);
        entity.rotationX154 = 0.0f;
        const std::int32_t remaining = static_cast<std::int32_t>(entity.staggerTimer12a) -
                                       static_cast<std::int32_t>(entity.pendingDamageBe);
        entity.halfword04 = cleared;
        entity.staggerTimer12a = static_cast<std::uint16_t>(remaining);
        if (static_cast<std::int16_t>(entity.staggerTimer12a) < 1)
        {
          entity.spawnParam94 = 0;
          entity.halfword04 = static_cast<std::uint16_t>(cleared | 0x10u);
          FUN_00225bf0_set_state_and_animation(entity, 5, 1);
          entity.hitFlagsC2 = 0;
        }
        else
        {
          entity.fadeRamp62 = kFUN_00276c30_flinchTicks;
          FUN_00225bf0_set_state_and_animation(entity, 4, 2);
          entity.hitFlagsC2 = 0;
        }
        entity.pendingDamageBe = 0;
      }
    }

    const std::uint32_t handler = environment.dispatchTable->stateHandler(
        kPTR_FUN_00325868_swarmStates, kSwarmStateCount, entity.state60);
    const bool implemented = entity.state60 >= 0 && entity.state60 <= 6;
    trace.recordStateDispatch(entity.typeId00, entity.state60, handler, implemented);

    switch (entity.state60)
    {
    case 0:
      FUN_00276de0_state0_init(entity, slot, environment);
      break;
    case 1:
      FUN_00276f50_state1_walk(entity, environment);
      break;
    case 2:
      FUN_00277110_state2_pick_mark(entity, environment);
      break;
    case 3:
      FUN_00277410_state3_mill(entity, slot, environment);
      break;
    case 4:
      FUN_00277860_state4_flinch(entity, environment);
      break;
    case 5:
      LAB_00277828_state5_death(entity);
      break;
    case 6:
      FUN_002778b0_state6_leap(entity, slot, environment);
      break;
    default:
      break;
    }

    // Out of the world in either direction and it is simply removed. Nothing
    // else in this type's states puts a floor under +0x28, so a leap that
    // carries one off the map ends here.
    if (entity.positionY28 < -kFUN_00276c30_heightLimit ||
        kFUN_00276c30_heightLimit < entity.positionY28)
    {
      FUN_00248040_release_record(entity, slot, environment);
      publish();
      FUN_00265ec0_destroy_entity(slot, environment);
      return;
    }

    publish();
  }

} // namespace orphen::ported::entity
