#include "ported/entity/original_tentacle_monster.h"

#include "ported/entity/actor_dispatch_table.h"
#include "ported/entity/original_enemy_attack.h"
#include "ported/model/psc3_skeleton.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace orphen::ported::entity
{
  namespace
  {
    using View = ActorEnvironment::BattleActorView;

    // The tuning, read out of SLUS_200.11 at 0x00353764..0x00353798.
    inline constexpr float kFGpffff97f4_turnRate = 0.0026179934f;    // per tick: 4.8 deg a frame
    inline constexpr float kDAT_00353768_grabConeMax = 0.5235987f;   // 30 degrees
    inline constexpr float kDAT_0035376c_grabConeMin = -0.5235987f;
    inline constexpr float kDAT_00353770_lungeStep = 0.074074075f;   // distance per frame
    inline constexpr float kDAT_00353774_lungeHeight = 1.2f;         // above the target's feet
    inline constexpr float kDAT_00353778_lungeSpeed = 0.0023148148f; // per tick
    inline constexpr float kDAT_0035377c_dropHeight = 0.05f;
    inline constexpr float kFGpffff9818_returnFlyThreshold = 0.2f;
    inline constexpr float kFGpffff981c_returnFlyStep = 0.074074075f;
    inline constexpr float kFGpffff9820_returnRiseStep = 0.074074075f;
    inline constexpr float kFGpffff9824_returnSpeed = 0.0023148148f; // per tick
    inline constexpr float kFGpffff9828_riseSpin = 0.0075049144f;    // per tick
    // FUN_00296c88's literal: close enough to the path to ride it from here.
    inline constexpr float kRejoinDistance = 0.5f;
    // FUN_00296398's literals: the grab's reach, and how far below bone 18 the
    // victim is put for the hit before it is set down.
    inline constexpr float kGrabReach = 2.0f;
    inline constexpr float kReleaseHitDrop = 0.5f;

    inline constexpr std::uint16_t kLungeCue = 0x1ED;
    inline constexpr std::uint16_t kGrabCue = 0x1EE;
    inline constexpr std::uint16_t kChewCue = 0x1EF;
    inline constexpr std::uint16_t kHitCue = 0x1F3;   // 499
    inline constexpr std::uint16_t kDeathCue = 0x1F4; // 500

    // The victim rides bone 20 while it is held, and is set down at bone 18.
    inline constexpr std::int8_t kHoldBone = 0x14;
    inline constexpr std::size_t kReleaseBone = 0x12;

    inline constexpr std::uint16_t kEntered = 0x4000;
    inline constexpr std::uint16_t kStateMask = 0xBFFF;

    // FUN_00248e48: n truncated to eleven bits, times 32.
    std::uint16_t FUN_00248e48_arm(std::int32_t n)
    {
      return static_cast<std::uint16_t>(
          static_cast<std::int32_t>(static_cast<std::uint32_t>(n) << 21) >> 16);
    }

    // FUN_00248e58: one frame off a tick timer, floored at zero.
    std::uint16_t FUN_00248e58_step(std::uint16_t timer, std::uint32_t frameTicks)
    {
      if (timer == 0)
      {
        return 0;
      }
      const auto next = static_cast<std::uint16_t>(timer - frameTicks);
      return timer < next ? 0 : next;
    }

    // FUN_00248e98: change the animation only if it differs, and drop +0x06
    // bits 0 and 4 either way.
    void FUN_00248e98_set_animation(OriginalEntity &entity, std::uint16_t animation)
    {
      if (entity.animationA0 != animation)
      {
        FUN_00225bc8_set_animation(entity, animation);
      }
      entity.flags06 = static_cast<std::uint16_t>(entity.flags06 & 0xFFEEu);
    }

    // FUN_0030bd20, the soft-float truncation: NaN and anything under one in
    // magnitude are 0, and it saturates rather than wrapping.
    std::int32_t FUN_0030bd20_truncate(float value)
    {
      if (std::isnan(value))
      {
        return 0;
      }
      if (value >= 2147483648.0f)
      {
        return std::numeric_limits<std::int32_t>::max();
      }
      if (value <= -2147483648.0f)
      {
        return std::numeric_limits<std::int32_t>::min();
      }
      return static_cast<std::int32_t>(value);
    }

    // FUN_00305408 over (to - from), the bearing every enemy aims with.
    float bearing(const OriginalEntity &from, const OriginalEntity &to)
    {
      return std::atan2(to.positionZ24 - from.positionZ24, to.positionX20 - from.positionX20);
    }

    struct Context
    {
      OriginalEntity &entity;
      std::size_t slot;
      const ActorEnvironment &environment;
      View &view;
      // sGpffffbe3c, the record's +0x2C copied out by the wrapper: the pool
      // slot the monster is aimed at.
      std::int16_t target;

      void sound(std::uint16_t cue) const
      {
        if (environment.FUN_00267d38_playSound)
        {
          environment.FUN_00267d38_playSound(cue, entity);
        }
      }
      OriginalEntity *targetEntity() const
      {
        if (target < 0 || static_cast<std::size_t>(target) >= environment.entityPool->slotCount())
        {
          return nullptr;
        }
        return &environment.entityPool->slot(static_cast<std::size_t>(target));
      }
      // FUN_00266ce8 at +0x1C0 / the record's +0x08.
      std::optional<orphen::ported::psm2::Vec3> pathPoint() const
      {
        if (!environment.FUN_00266ce8_sample_path || !environment.pathLapTicks)
        {
          return std::nullopt;
        }
        const std::uint32_t lap = environment.pathLapTicks(entity.tentaclePath1bc);
        return environment.FUN_00266ce8_sample_path(
            entity.tentaclePath1bc,
            static_cast<float>(entity.tentaclePathTicks1c0) / static_cast<float>(lap));
      }
    };

    // FUN_00296218, states 0 and 1.
    std::int32_t FUN_00296218_init(Context &context)
    {
      OriginalEntity &entity = context.entity;
      const ActorEnvironment &environment = context.environment;
      entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 8u);

      // FUN_0025bae8(0, type, r): the enemy table's row, and the same seven
      // fields FUN_0023a518 copies.
      if (environment.uGpffffadf8_stats != nullptr)
      {
        const auto record = environment.uGpffffadf8_stats->FUN_00229688_record(
            0, static_cast<std::int32_t>(entity.typeId00) - 0x7C);
        if (record.has_value())
        {
          entity.radius54 = record->radius0c;
          entity.height58 = record->height10;
          const auto hitPoints =
              static_cast<std::int16_t>(static_cast<std::int8_t>(record->byte06));
          entity.staggerTimer12a = static_cast<std::uint16_t>(hitPoints);
          entity.attackPower12c = static_cast<std::uint16_t>(
              static_cast<std::int16_t>(static_cast<std::int8_t>(record->byte07)));
          entity.defence12e = static_cast<std::uint16_t>(
              static_cast<std::int16_t>(static_cast<std::int8_t>(record->byte08)));
          entity.hitVolumeRadius11c = record->radius0c;
          entity.hitVolumeHeight120 = record->height10;
          entity.maxHitPoints128 = static_cast<std::uint16_t>(hitPoints);
        }
      }

      FUN_00216078_fill_attack_records(static_cast<std::int16_t>(entity.typeId00),
                                       DAT_0058b180_enemy93Attacks(), environment);

      if (environment.FUN_0023f8b8_bind_battle_actor)
      {
        entity.battleActorRecord198 = environment.FUN_0023f8b8_bind_battle_actor(context.slot);
      }
      entity.state60 = 0x4006;
      // FUN_00266050: the first free light, recorded but not lit.
      entity.lightSlot195 =
          environment.DAT_00343888_lights != nullptr
              ? static_cast<std::int8_t>(environment.DAT_00343888_lights->FUN_00266050_allocateFromZero())
              : static_cast<std::int8_t>(-1);
      entity.tentaclePath1bc =
          environment.FUN_002475b8_find_path
              ? environment.FUN_002475b8_find_path(static_cast<std::int16_t>(entity.typeId00),
                                                   static_cast<std::int8_t>(entity.byte95), 0, 0, 0)
              : 0u;
      if ((environment.DAT_00354fc2_battleState & 1u) != 0 && environment.FUN_002d8948_attach_cursor)
      {
        environment.FUN_002d8948_attach_cursor(context.slot);
      }
      return 0;
    }

    // FUN_00296398, state 2: the grab.
    std::int32_t FUN_00296398_grab(Context &context)
    {
      OriginalEntity &entity = context.entity;
      const std::uint32_t ticks = context.environment.frameTicks;
      if (entity.tentaclePath1bc == 0)
      {
        return 0;
      }
      OriginalEntity *target = context.targetEntity();

      if ((entity.state60 & kEntered) != 0)
      {
        if (target == nullptr)
        {
          context.view.pendingAction0e = 6;
          return 0;
        }
        const float offset = orphen::ported::model::FUN_002166e8_angle_delta(
            entity.facingRadians5c, bearing(entity, *target));
        entity.tentacleHeading1a4 = offset;
        // Outside the cone: stay entered and try again next frame, while the
        // wrapper keeps turning toward the target.
        if (kDAT_00353768_grabConeMax <= offset || offset <= kDAT_0035376c_grabConeMin)
        {
          entity.tentaclePhase19c = 0;
          return 1;
        }
        entity.tentaclePhase19c = 0;
        entity.state60 = static_cast<std::uint16_t>(entity.state60 & kStateMask);
        const float distance = std::hypot(target->positionZ24 - entity.positionZ24,
                                          target->positionX20 - entity.positionX20);
        const auto frames = static_cast<std::int16_t>(FUN_0030bd20_truncate(
            ((distance - 0.5f) + target->radius54) / kDAT_00353770_lungeStep));
        entity.fadeRamp62 = FUN_00248e48_arm(frames);
        // A bare store, not FUN_00225bc8: the clip keeps its place.
        entity.animationA0 = 2;
        entity.tentacleClimb1ac =
            ((target->positionY28 + kDAT_00353774_lungeHeight) - entity.positionY28) /
            static_cast<float>(static_cast<std::int32_t>(frames) << 5);
      }

      if (entity.tentaclePhase19c == 0)
      {
        // The flight in, facing wherever the wrapper has it turned.
        if (entity.fadeRamp62 != 0)
        {
          entity.fadeRamp62 = FUN_00248e58_step(entity.fadeRamp62, ticks);
          const float speed = static_cast<float>(ticks) * kDAT_00353778_lungeSpeed;
          entity.desiredDeltaX30 = speed * std::cos(entity.facingRadians5c);
          entity.desiredDeltaZ34 = speed * std::sin(entity.facingRadians5c);
          entity.animationA0 = 2;
          entity.desiredDeltaY38 = entity.tentacleClimb1ac * static_cast<float>(ticks);
          if ((entity.flags06 & 1u) != 0)
          {
            context.sound(kLungeCue);
          }
          return 1;
        }
        if (entity.animationA0 != 0x13)
        {
          FUN_00225bc8_set_animation(entity, 0x13);
          return 1;
        }
        if ((entity.flags06 & 1u) == 0)
        {
          return 1;
        }
        // A target the wrapper could aim at but this cannot reach falls to
        // the miss below.
        if (target != nullptr)
        {
          const float dx = target->positionX20 - entity.positionX20;
          const float dz = target->positionZ24 - entity.positionZ24;
          const float dy = target->positionY28 - entity.positionY28;
          if (target->parentSlot192 == -1 && (target->battleFlags96 & 4u) == 0 &&
              std::sqrt(dx * dx + dz * dz + dy * dy) < kGrabReach)
          {
            entity.positionX20 = target->positionX20;
            entity.positionZ24 = target->positionZ24;
            entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 0x10u);
            target->battleFlags96 = static_cast<std::uint8_t>(target->battleFlags96 | 4u);
            entity.fadeRamp62 = 0;
            entity.tentaclePhase19c = 2;
            context.sound(kGrabCue);
            FUN_00225bc8_set_animation(entity, 0x12);
            return 1;
          }
        }
        entity.state60 = 0x4008;
        context.view.currentAction0f = 8;
        FUN_00225bc8_set_animation(entity, 0);
      }

      if (entity.tentaclePhase19c != 2 || target == nullptr)
      {
        return 1;
      }

      // The hold. The victim becomes a child of bone 20, at the bone's origin.
      if (target->parentSlot192 == -1)
      {
        target->attachBone194 = kHoldBone;
        target->positionX20 = 0.0f;
        target->positionZ24 = 0.0f;
        target->positionY28 = 0.0f;
        target->facingRadians5c = 0.0f;
        target->parentSlot192 = static_cast<std::int16_t>(context.slot & 0xFFu);
      }
      if (entity.fadeRamp62 == 0)
      {
        if ((entity.flags06 & 1u) == 0)
        {
          return 1;
        }
        // Animation 0x12 is over: let go at bone 18 and land the hit.
        target->parentSlot192 = -1;
        target->attachBone194 = 0;
        orphen::ported::psm2::Vec3 drop{};
        if (context.environment.FUN_0020dc88_bone_point)
        {
          drop = context.environment.FUN_0020dc88_bone_point(context.slot, kReleaseBone, {});
        }
        target->positionX20 = drop.x;
        target->positionZ24 = drop.y;
        target->battleFlags96 = static_cast<std::uint8_t>(target->battleFlags96 & 0xFBu);
        target->positionY28 = drop.z - kReleaseHitDrop;
        entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 & 0xFFEFu);
        FUN_00216128_direct_hit(entity, context.slot, *target,
                                DAT_0058b180_enemy93Attacks().record[0], context.environment);
        entity.state60 = 0x4008;
        target->positionY28 = drop.z + kDAT_0035377c_dropHeight;
        context.view.currentAction0f = 8;
        FUN_00225bc8_set_animation(entity, 0);
        return 1;
      }
      // Unreachable as shipped -- +0x62 is zeroed by the grab and nothing in
      // the hold arms it -- but it is the original's.
      entity.fadeRamp62 = FUN_00248e58_step(entity.fadeRamp62, ticks);
      if ((entity.flagsAa & 0x100u) != 0 && (entity.flags06 & 8u) != 0)
      {
        context.sound(kChewCue);
      }
      return 1;
    }

    // FUN_00296338, states 4 and 5.
    std::int32_t FUN_00296338_flourish(Context &context)
    {
      OriginalEntity &entity = context.entity;
      if ((entity.state60 & kEntered) != 0)
      {
        entity.state60 = static_cast<std::uint16_t>(entity.state60 & kStateMask);
        FUN_00225bc8_set_animation(entity, 6);
      }
      if ((entity.flags06 & 1u) != 0)
      {
        entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10u);
        context.view.pendingAction0e = 6;
      }
      return 1;
    }

    // FUN_00296c88, state 6: idle until the animation comes round, then either
    // ride the path from here or fly back to it first.
    std::int32_t FUN_00296c88_idle(Context &context)
    {
      OriginalEntity &entity = context.entity;
      if ((entity.state60 & kEntered) != 0)
      {
        entity.state60 = static_cast<std::uint16_t>(entity.state60 & kStateMask);
        if ((entity.flags06 & 0x10u) != 0)
        {
          FUN_00248e98_set_animation(entity, 0);
        }
      }
      if (entity.tentaclePath1bc == 0 || (entity.flags06 & 1u) == 0)
      {
        return 0;
      }
      const auto point = context.pathPoint();
      FUN_00225bc8_set_animation(entity, 0);
      if (!point.has_value())
      {
        return 0;
      }
      const float distance = std::hypot(point->x - entity.positionX20, point->y - entity.positionZ24);
      if (kRejoinDistance < distance)
      {
        context.view.pendingAction0e = 8;
        return 1;
      }
      entity.state60 = 0x400E;
      return 0;
    }

    // FUN_00296dc8, state 8: back to the path. Far away it flies there level
    // and climbs as it goes; close in it only rises, spinning, and the wrapper
    // stops turning it for as long as that takes.
    std::int32_t FUN_00296dc8_return(Context &context)
    {
      OriginalEntity &entity = context.entity;
      const auto ticks = static_cast<float>(context.environment.frameTicks);
      if (entity.tentaclePath1bc == 0)
      {
        return 0;
      }
      const auto point = context.pathPoint();
      if (!point.has_value())
      {
        return 0;
      }
      if ((entity.state60 & kEntered) != 0)
      {
        entity.state60 = static_cast<std::uint16_t>(entity.state60 & kStateMask);
        const float distance =
            std::hypot(point->y - entity.positionZ24, point->x - entity.positionX20);
        if (kFGpffff9818_returnFlyThreshold < distance)
        {
          entity.tentaclePhase19c = 4;
          const auto frames = static_cast<std::int16_t>(
              FUN_0030bd20_truncate(distance / kFGpffff981c_returnFlyStep));
          entity.fadeRamp62 = FUN_00248e48_arm(frames);
          entity.tentacleClimb1ac = (point->z - entity.positionY28) /
                                    static_cast<float>(static_cast<std::int32_t>(frames) << 5);
          entity.tentacleHeading1a4 =
              std::atan2(point->y - entity.positionZ24, point->x - entity.positionX20);
        }
        else
        {
          const float rise = point->z - entity.positionY28;
          entity.tentaclePhase19c = 5;
          const auto frames = static_cast<std::int16_t>(
              FUN_0030bd20_truncate(std::fabs(rise) / kFGpffff9820_returnRiseStep));
          entity.tentacleClimb1ac =
              rise / static_cast<float>(static_cast<std::int32_t>(frames) << 5);
          entity.fadeRamp62 = FUN_00248e48_arm(frames);
        }
      }
      if (entity.fadeRamp62 == 0)
      {
        context.view.pendingAction0e = 6;
        entity.tentacleFlags19f = static_cast<std::uint8_t>(entity.tentacleFlags19f & 0xFEu);
        return 1;
      }
      entity.fadeRamp62 = FUN_00248e58_step(entity.fadeRamp62, context.environment.frameTicks);
      if (entity.tentaclePhase19c == 4)
      {
        const float speed = ticks * kFGpffff9824_returnSpeed;
        entity.desiredDeltaX30 = speed * std::cos(entity.tentacleHeading1a4);
        entity.desiredDeltaZ34 = speed * std::sin(entity.tentacleHeading1a4);
        entity.desiredDeltaY38 = entity.tentacleClimb1ac * ticks;
      }
      else
      {
        entity.tentacleFlags19f = static_cast<std::uint8_t>(entity.tentacleFlags19f | 1u);
        entity.desiredDeltaY38 = entity.tentacleClimb1ac * ticks;
        entity.facingRadians5c += ticks * kFGpffff9828_riseSpin;
      }
      return 1;
    }

    // FUN_00297088, state 12: the hit.
    std::int32_t FUN_00297088_hit(Context &context)
    {
      OriginalEntity &entity = context.entity;
      if (entity.freezeTimerBd != 0)
      {
        entity.freezeTimerBd = 0;
      }
      if ((entity.state60 & kEntered) != 0)
      {
        entity.tentaclePhase19c = 0;
        entity.state60 = static_cast<std::uint16_t>(entity.state60 & kStateMask);
        FUN_00225bc8_set_animation(entity, 8);
        const std::int32_t remaining = static_cast<std::int32_t>(entity.staggerTimer12a) -
                                       static_cast<std::int32_t>(entity.pendingDamageBe);
        entity.staggerTimer12a = static_cast<std::uint16_t>(remaining);
        if (static_cast<std::int16_t>(entity.staggerTimer12a) < 1)
        {
          // +0xBE is left standing: state 13 never looks at it, and the action
          // check stops reading it once the current action is 13.
          entity.state60 = 0x400D;
          context.view.currentAction0f = 0x0D;
          return 1;
        }
      }
      entity.hitFlagsC2 = 0;
      entity.pendingDamageBe = 0;
      if ((entity.flags06 & 1u) == 0)
      {
        return 1;
      }
      context.sound(kHitCue);
      entity.state60 = 0x4008;
      context.view.currentAction0f = 8;
      return 0;
    }

    // FUN_00297148, state 13: the death. +0x134 runs down from 0x7F a step a
    // frame and the slot goes at 3.
    std::int32_t FUN_00297148_death(Context &context)
    {
      OriginalEntity &entity = context.entity;
      if ((entity.state60 & kEntered) != 0)
      {
        entity.fadeLevel134 = 0x7F;
        entity.state60 = static_cast<std::uint16_t>(entity.state60 & kStateMask);
        context.sound(kDeathCue);
        FUN_00225bc8_set_animation(entity, 4);
      }
      if ((entity.flagsAa & 0x100u) != 0 && (entity.flags06 & 8u) != 0)
      {
        context.sound(kDeathCue);
      }
      entity.fadeLevel134 = static_cast<std::uint8_t>(entity.fadeLevel134 - 1);
      if ((entity.flags06 & 1u) != 0)
      {
        entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10u);
      }
      if (entity.fadeLevel134 < 4)
      {
        FUN_00265ec0_destroy_entity(context.slot, context.environment);
      }
      return 1;
    }

    // FUN_00297200, state 14: ride the path. The whole step to the lap's
    // current point is the movement request, so the physics puts it there.
    std::int32_t FUN_00297200_ride(Context &context)
    {
      OriginalEntity &entity = context.entity;
      if (entity.tentaclePath1bc == 0)
      {
        context.view.pendingAction0e = 6;
        return 0;
      }
      entity.state60 = static_cast<std::uint16_t>(entity.state60 & kStateMask);
      const auto point = context.pathPoint();
      if (point.has_value())
      {
        entity.desiredDeltaX30 = point->x - entity.positionX20;
        entity.desiredDeltaZ34 = point->y - entity.positionZ24;
        entity.desiredDeltaY38 = point->z - entity.positionY28;
      }
      context.view.currentAction0f = 6;
      if ((entity.collisionFlags0c & 0x60u) == 0)
      {
        entity.tentaclePathTicks1c0 += context.environment.frameTicks;
        if (context.environment.pathLapTicks &&
            context.environment.pathLapTicks(entity.tentaclePath1bc) <= entity.tentaclePathTicks1c0)
        {
          entity.tentaclePathTicks1c0 = 0;
        }
      }
      return 0;
    }

    // FUN_00297348: the pending order, turned into a state.
    void FUN_00297348_action_check(Context &context)
    {
      OriginalEntity &entity = context.entity;
      View &view = context.view;
      if (entity.verticalVelocity44 == 0.0f)
      {
        entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 8u);
      }
      // Hit or dying: orders are dropped.
      if (static_cast<std::uint8_t>(view.currentAction0f - 0x0C) < 2)
      {
        view.pendingAction0e = 0;
        return;
      }
      if (view.pendingAction0e == 6)
      {
        entity.state60 = static_cast<std::uint16_t>(6u | kEntered);
        view.currentAction0f = 6;
        view.pendingAction0e = 0;
      }
      if (static_cast<std::uint8_t>(view.pendingAction0e - 10) < 2)
      {
        view.currentAction0f = view.pendingAction0e;
        entity.flags06 = static_cast<std::uint16_t>(entity.flags06 & 0xFFEFu);
        FUN_00248e98_set_animation(entity, entity.animationA0);
        view.pendingAction0e = 0;
      }
      if (view.pendingAction0e == 1)
      {
        entity.state60 = static_cast<std::uint16_t>(1u | kEntered);
        view.pendingAction0e = 0;
      }
      if (static_cast<std::int16_t>(entity.pendingDamageBe) != 0)
      {
        view.currentAction0f = 0x0C;
        entity.state60 = 0x400C;
        view.pendingAction0e = 0;
        return;
      }
      // Actions 2, 3 and 4 are all the grab.
      if (static_cast<std::uint8_t>(view.pendingAction0e - 2) < 3)
      {
        view.pendingAction0e = 2;
        view.currentAction0f = 2;
        entity.state60 = static_cast<std::uint16_t>(2u | kEntered);
        view.pendingAction0e = 0;
      }
      if (4 < view.pendingAction0e)
      {
        view.currentAction0f = view.pendingAction0e;
        entity.state60 = static_cast<std::uint16_t>(view.pendingAction0e | kEntered);
        view.pendingAction0e = 0;
      }
    }

    // Returns the handler's own result, and whether the port has a body for
    // it. State 3 is the only one that does not, and nothing reaches it.
    std::int32_t dispatch(Context &context, std::uint16_t state, bool &implemented)
    {
      implemented = true;
      switch (state)
      {
      case 0:
      case 1:
        return FUN_00296218_init(context);
      case 2:
        return FUN_00296398_grab(context);
      case 4:
      case 5:
        return FUN_00296338_flourish(context);
      case 6:
        return FUN_00296c88_idle(context);
      case 8:
        return FUN_00296dc8_return(context);
      case 12:
        return FUN_00297088_hit(context);
      case 13:
        return FUN_00297148_death(context);
      case 14:
        return FUN_00297200_ride(context);
      case 7:
      case 9:
      case 10:
      case 11:
      case 15:
      case 16:
        return 0;
      default:
        implemented = false;
        return 0;
      }
    }
  } // namespace

  void FUN_00296030_tentacle93(OriginalEntity &entity,
                               std::size_t slot,
                               const ActorEnvironment &environment,
                               ActorTrace &trace)
  {
    if (environment.entityPool == nullptr || environment.dispatchTable == nullptr)
    {
      return;
    }

    const std::uint16_t flags04 = entity.halfword04;
    entity.halfword04 = static_cast<std::uint16_t>(flags04 & 0xFFFEu);
    if ((flags04 & 0x10u) != 0 && entity.pendingDamageBe != 0)
    {
      entity.pendingDamageBe = 0;
    }
    if (FUN_0023a068_freeze_gate(entity, environment.frameTicks))
    {
      return;
    }

    // An unbound monster has a null +0x198 on hardware, which the record
    // writes below would fault on; a ported one is always bound by state 0
    // before it gets there, so the scratch view only ever absorbs state 0.
    View view;
    const bool haveRecord = static_cast<bool>(environment.DAT_00354eb4_battleActor) &&
                            environment.DAT_00354eb4_battleActor(entity.battleActorRecord198, view);
    Context context{entity, slot, environment, view, view.target2c};

    if (entity.state60 == 0)
    {
      entity.freezeTimerBd = 0;
    }
    else
    {
      view.flags38 &= ~1u;
      if (view.pendingAction0e == 0x0B)
      {
        // Action 11 parks it: latch once and do nothing else.
        if (view.currentAction0f != 0x0B)
        {
          entity.flags06 = static_cast<std::uint16_t>(entity.flags06 & 0xFFEFu);
          view.currentAction0f = 0x0B;
          entity.guardArc124 = 0.0f;
        }
        if (haveRecord && environment.DAT_00354eb4_setBattleActor)
        {
          environment.DAT_00354eb4_setBattleActor(entity.battleActorRecord198, view);
        }
        return;
      }
      if (const OriginalEntity *target = context.targetEntity();
          target != nullptr && (entity.tentacleFlags19f & 1u) == 0)
      {
        const float wanted = bearing(entity, *target);
        if (entity.facingRadians5c != wanted)
        {
          const float step = FUN_0023a320_approach_angle(
              entity.facingRadians5c, wanted,
              static_cast<float>(environment.frameTicks) * kFGpffff97f4_turnRate);
          if (step == 0.0f)
          {
            entity.facingRadians5c = wanted;
          }
          entity.facingRadians5c += step;
        }
      }
      view.flags38 &= ~1u;
      FUN_00297348_action_check(context);
    }

    const std::uint16_t state = static_cast<std::uint16_t>(entity.state60 & kStateMask);
    bool implemented = true;
    const std::uint32_t handlerAddress = environment.dispatchTable->stateHandler(
        kPTR_FUN_00325c80_enemy93States, kEnemy93StateCount, state);
    const bool wasStateZero = entity.state60 == 0;
    const std::int32_t result = dispatch(context, state, implemented);
    trace.recordStateDispatch(entity.typeId00, state, handlerAddress, implemented);

    // State 0 ran against a record it had not bound yet; its result is 0, so
    // the original's write through the stale global is a no-op too.
    if (wasStateZero || !haveRecord)
    {
      return;
    }
    if (result != 0)
    {
      view.flags38 |= 1u;
    }
    if (environment.DAT_00354eb4_setBattleActor)
    {
      environment.DAT_00354eb4_setBattleActor(entity.battleActorRecord198, view);
    }
  }

} // namespace orphen::ported::entity
