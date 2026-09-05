#include "ported/entity/original_battle_enemy.h"

#include "ported/entity/actor_dispatch_table.h"
#include "ported/model/psc3_skeleton.h"

#include <array>
#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    // fGpffff92c8 / fGpffff95b8, both 0.17453289 -- ten degrees. Multiplied by
    // the frame tick and by 0.03125, so a nominal 0x20-tick frame turns an
    // enemy exactly ten degrees. Two symbols, one value, kept apart because
    // they are two different types' constants.
    inline constexpr float kFGpffff92c8_enemy80TurnRate = 0.17453289031982422f;
    inline constexpr float kFGpffff95b8_enemy8aTurnRate = 0.17453289031982422f;

    // DAT_0035322c, the nudge FUN_0027f288 gives a type 0x80 as it dies.
    inline constexpr float kDAT_0035322c_deathNudge = 0.0010000000474974513f;

    // FUN_00280850's constants: fGpffff92e8 (a full turn, the degrees-to-radians
    // numerator), the four bones it drives and the roll it gives each of them.
    inline constexpr float kFGpffff92e8_tau = 6.283184051513672f;
    inline constexpr std::array<std::size_t, 4> kDAT_003259a8_wobbleBones{{3, 4, 5, 6}};
    inline constexpr std::array<float, 4> kDAT_00325998_wobbleRoll{{0.85f, 0.15f, -0.85f, -0.15f}};

    // The two cues these types key directly. 0x1E3 is the ambient the type 0x80
    // rolls for while it is acting; 0x1C8 is the type 0x8A's step, keyed when
    // its animation 1 comes round.
    inline constexpr std::uint16_t kFUN_0027f288_ambientCue = 0x1E3;
    inline constexpr std::uint16_t kFUN_0028a958_stepCue = 0x1C8;

    // FUN_0023a678: a tick countdown, floored at zero rather than allowed
    // negative. Shared by both types' state 1.
    std::int16_t FUN_0023a678_countdown(std::int16_t timer, std::uint32_t frameTicks)
    {
      const std::int16_t remaining =
          static_cast<std::int16_t>(timer - static_cast<std::int16_t>(frameTicks));
      return remaining < 1 ? static_cast<std::int16_t>(0) : remaining;
    }

    // FUN_0023a958: the entity an enemy is aimed at -- the actor record's
    // +0x2C, as a pool slot. A negative one falls back to DAT_0058beb0, pool
    // slot 0, so "no target" means the player rather than "no angle". That
    // fallback is the whole reason an idle enemy faces Orphen.
    const OriginalEntity &FUN_0023a958_target(const EntityPool &pool, std::int16_t target)
    {
      if (target < 0 || static_cast<std::size_t>(target) >= pool.slotCount())
      {
        return pool.slot(0);
      }
      return pool.slot(static_cast<std::size_t>(target));
    }

    // FUN_0023a4b8: the bearing from one entity to another. FUN_0023a480 is the
    // same call with pool slot 0 read straight out of DAT_0058bed0/DAT_0058bed4.
    float FUN_0023a4b8_bearing(const OriginalEntity &from, const OriginalEntity &to)
    {
      return std::atan2(to.positionZ24 - from.positionZ24, to.positionX20 - from.positionX20);
    }

    // FUN_0023a518, inlined into every enemy's state 0. The port's script-side
    // copy writes the same seven fields; this one is here because an enemy
    // reaches it through FUN_0025bae8(0, type) rather than through a party
    // record.
    void FUN_0023a518_apply(OriginalEntity &entity,
                            const orphen::ported::resource::StatRecord &record)
    {
      entity.radius54 = record.radius0c;
      entity.hitVolumeRadius11c = record.radius0c;
      entity.height58 = record.height10;
      entity.hitVolumeHeight120 = record.height10;
      const auto hitPoints = static_cast<std::int16_t>(static_cast<std::int8_t>(record.byte06));
      entity.staggerTimer12a = static_cast<std::uint16_t>(hitPoints);
      entity.maxHitPoints128 = static_cast<std::uint16_t>(hitPoints);
      entity.attackPower12c =
          static_cast<std::uint16_t>(static_cast<std::int16_t>(static_cast<std::int8_t>(record.byte07)));
      entity.defence12e =
          static_cast<std::uint16_t>(static_cast<std::int16_t>(static_cast<std::int8_t>(record.byte08)));
    }

    // The state 0 both types share, line for line: FUN_0027f978 and FUN_0028ae10
    // differ only in which scratch words FUN_00216078 is asked to fill and in
    // the trailing +0x1AC = 1 the 0x8A adds.
    //
    // The three FUN_00216078 calls are deliberately absent. They copy a type's
    // first three attack records into globals at 0x005739B0 / 0x0058B140 that
    // nothing in the executable reads back; their only lasting effect is
    // DAT_00354C64, the record count, which FUN_0023f8b8 re-establishes for
    // itself on the next line.
    void enemy_state0(OriginalEntity &entity,
                      std::size_t slot,
                      const ActorEnvironment &environment)
    {
      entity.scale14c = 1.0f;
      entity.scaleZ150 = 1.0f;

      // FUN_0025bae8(0, type, r): group 0 of SCR.BIN 0xBF indexed by
      // `type - 0x7C`, which is the enemy table. Not group 2 -- that one holds
      // ids 0x62..0x79 and knows nothing about a 0x80 or a 0x8A.
      if (environment.uGpffffadf8_stats != nullptr)
      {
        const auto record = environment.uGpffffadf8_stats->FUN_00229688_record(
            0, static_cast<std::int32_t>(entity.typeId00) - 0x7C);
        if (record.has_value())
        {
          FUN_0023a518_apply(entity, *record);
        }
      }

      // +0x19C starts as the placement's own facing. Everything after this
      // re-aims it; this is the only frame it is the authored angle.
      entity.battleDesiredFacing19c = entity.facingRadians5c;
      entity.battleFlags96 = static_cast<std::uint8_t>(entity.battleFlags96 | 1u);

      FUN_00225bf0_set_state_and_animation(entity, 1, 0);

      // FUN_0023f8b8, from the caller the original really uses. The port also
      // calls it at the spawn, for the enemy types whose state 0 is not ported
      // yet; the second call is idempotent.
      if (environment.FUN_0023f8b8_bind_battle_actor)
      {
        entity.battleActorRecord198 = environment.FUN_0023f8b8_bind_battle_actor(slot);
      }
    }

    // FUN_0027f5c0 and FUN_0028ac38, the idle default: aim at the target, roll a
    // 100..199-tick hold, drop into state 1 on an idle animation and mark the
    // record's current action 6.
    //
    // The two differ in where the angle comes from and in the animation roll.
    // The 0x80 goes through FUN_0023a958, so it honours a target the battle
    // script picked; the 0x8A calls FUN_0023a480, which is pool slot 0
    // unconditionally -- it always faces the player.
    void enemy_idle_default(OriginalEntity &entity,
                            const ActorEnvironment &environment,
                            bool useRecordTarget,
                            ActorEnvironment::BattleActorView &view)
    {
      const EntityPool &pool = *environment.entityPool;
      const OriginalEntity &target =
          useRecordTarget ? FUN_0023a958_target(pool, view.target2c) : pool.slot(0);
      entity.battleDesiredFacing19c = FUN_0023a4b8_bearing(entity, target);

      const std::int32_t roll =
          environment.random ? static_cast<std::int32_t>(environment.random()) : 0;
      entity.fadeRamp62 =
          static_cast<std::uint16_t>((static_cast<std::int16_t>(roll % 100) + 100) * 0x20);

      const std::uint32_t pick = environment.random ? environment.random() : 0;
      if (useRecordTarget)
      {
        // FUN_0027f5c0: one bit, animation 1 or 0.
        FUN_00225bf0_set_state_and_animation(entity, 1,
                                             static_cast<std::uint16_t>((pick & 1u) == 0 ? 1 : 0));
      }
      else
      {
        // FUN_0028ac38: two bits, animation 0, 2 or 3.
        const std::uint32_t bits = pick & 3u;
        FUN_00225bf0_set_state_and_animation(
            entity, 1, static_cast<std::uint16_t>(bits == 0 ? 0 : (bits == 1 ? 2 : 3)));
      }
      view.currentAction0f = 6;
    }

    // FUN_0027f4b0 / FUN_0028ab28. Returns the original's 1 -- "this enemy is
    // out of the fight, skip the hit reaction" -- as true.
    //
    // Actions 1..8 are the battle script's vocabulary: close, strike, back off,
    // hold. Their bodies (FUN_0027f5c8 / FUN_0028ac40) are the next slice and
    // nothing in the port can ask for one yet, because the PTR_LAB_0031D118 VM
    // that writes the pending byte is not stepped. What is here is the frame
    // every branch of the original shares: latch the busy bit, publish the
    // action, clear the request.
    bool enemy_action_check(OriginalEntity &entity,
                            const ActorEnvironment &environment,
                            ActorEnvironment::BattleActorView &view,
                            bool haveRecord,
                            bool useRecordTarget)
    {
      if (static_cast<std::int16_t>(entity.staggerTimer12a) < 1)
      {
        return true;
      }
      if (!haveRecord)
      {
        return false;
      }
      if (view.pendingAction0e != 0)
      {
        view.flags38 |= 1u;
        const std::uint8_t action = view.pendingAction0e;
        if (action == 0x0A)
        {
          // The one action that leaves the pending byte alone: it parks the
          // enemy in state 1 and reports itself busy.
          FUN_00225bf0_set_state_and_animation(entity, 1, 0);
          view.currentAction0f = 0x0A;
          return true;
        }
        if (action == 0x0B)
        {
          view.currentAction0f = 0x0B;
          return false;
        }
        view.currentAction0f = action;
        view.pendingAction0e = 0;
        return false;
      }
      if ((view.flags38 & 1u) != 0)
      {
        return false;
      }
      enemy_idle_default(entity, environment, useRecordTarget, view);
      return false;
    }

    // FUN_00280850: the type 0x80's idle wobble. Four bones given a fixed roll
    // each and swept together through a phase that runs -40 to 60 degrees at 25
    // a frame. Skipped on animations 4 and 5, which drive those bones
    // themselves.
    void FUN_00280850_wobble(OriginalEntity &entity,
                             std::size_t slot,
                             const ActorEnvironment &environment)
    {
      if (static_cast<std::uint16_t>(entity.animationA0 - 4) <= 1)
      {
        return;
      }
      if (slot < environment.boneOverrides.size())
      {
        auto &overrides = environment.boneOverrides[slot];
        for (std::size_t index = 0; index < kDAT_003259a8_wobbleBones.size(); ++index)
        {
          std::array<float, orphen::ported::model::kPoseFieldCount> pose{};
          pose[0] = (entity.enemyWobblePhase1d0 * kFGpffff92e8_tau) / 360.0f;
          pose[2] = kDAT_00325998_wobbleRoll[index];
          pose[6] = 1.0f;
          orphen::ported::model::FUN_0020d8c0_set_bone_override(
              overrides, kDAT_003259a8_wobbleBones[index], pose, 0);
        }
      }

      entity.enemyWobblePhase1d0 += static_cast<float>(environment.frameTicks) * 25.0f * 0.03125f;
      if (entity.enemyWobblePhase1d0 > 60.0f)
      {
        entity.enemyWobblePhase1d0 = -40.0f;
      }
    }

    // FUN_0027fa88, the type 0x80's state 1: hold the record busy while the
    // facing catches up with +0x19C, then spend the hold timer and release it,
    // which is what lets the idle default aim again.
    void FUN_0027fa88_enemy80_turn(OriginalEntity &entity,
                                   const ActorEnvironment &environment,
                                   ActorEnvironment::BattleActorView &view)
    {
      view.flags38 |= 1u;
      const float step =
          static_cast<float>(environment.frameTicks) * kFGpffff92c8_enemy80TurnRate * 0.03125f;
      const float delta = FUN_0023a320_approach_angle(entity.facingRadians5c,
                                                      entity.battleDesiredFacing19c, step);
      if (delta == 0.0f)
      {
        entity.fadeRamp62 = static_cast<std::uint16_t>(FUN_0023a678_countdown(
            static_cast<std::int16_t>(entity.fadeRamp62), environment.frameTicks));
        if (entity.fadeRamp62 == 0)
        {
          view.flags38 &= ~1u;
        }
      }
      else
      {
        entity.facingRadians5c += delta;
      }
    }

    // FUN_0028af28, the type 0x8A's state 1. It clears the busy bit rather than
    // setting it, so the idle default runs again on the very next frame: a 0x8A
    // with nothing driving it re-aims and re-rolls its idle animation every
    // frame. That is the original's own behaviour, and it does not show on
    // hardware because the per-actor battle script keeps the record busy.
    void FUN_0028af28_enemy8a_turn(OriginalEntity &entity,
                                   const ActorEnvironment &environment,
                                   ActorEnvironment::BattleActorView &view)
    {
      view.flags38 &= ~1u;
      entity.fadeRamp62 = static_cast<std::uint16_t>(FUN_0023a678_countdown(
          static_cast<std::int16_t>(entity.fadeRamp62), environment.frameTicks));
      if (entity.fadeRamp62 == 0)
      {
        return;
      }
      const float step =
          static_cast<float>(environment.frameTicks) * kFGpffff95b8_enemy8aTurnRate * 0.03125f;
      const float delta = FUN_0023a320_approach_angle(entity.facingRadians5c,
                                                      entity.battleDesiredFacing19c, step);
      if (delta != 0.0f)
      {
        entity.facingRadians5c += delta;
      }
    }
  } // namespace

  void FUN_0027f288_enemy80(OriginalEntity &entity,
                            std::size_t slot,
                            const ActorEnvironment &environment,
                            ActorTrace &trace)
  {
    if (environment.entityPool == nullptr || environment.dispatchTable == nullptr)
    {
      return;
    }

    entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 & 0xFFF9u);

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

    if (!enemy_action_check(entity, environment, view, haveRecord, true))
    {
      // FUN_0023a068 inlined: the last frozen frame still runs the state.
      if (FUN_0023a068_freeze_gate(entity, environment.frameTicks))
      {
        publish();
        return;
      }

      // +0xBE is damage taken since the last tick. Nothing in the port deals an
      // enemy any yet, so this is structure rather than behaviour.
      if (entity.pendingDamageBe != 0)
      {
        const std::int32_t remaining = static_cast<std::int32_t>(entity.staggerTimer12a) -
                                       static_cast<std::int32_t>(entity.pendingDamageBe);
        entity.staggerTimer12a = static_cast<std::uint16_t>(remaining);
        if (static_cast<std::int16_t>(entity.staggerTimer12a) < 1)
        {
          entity.halfword04 = static_cast<std::uint16_t>((entity.halfword04 & 0xFFF7u) | 0x10u);
          const std::uint32_t pick = environment.random ? environment.random() : 0;
          FUN_00225bf0_set_state_and_animation(entity, 7,
                                               static_cast<std::uint16_t>((pick & 1u) == 0 ? 5 : 4));
          entity.hitFlagsC2 = 0;
          entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 1u);
          entity.desiredDeltaX30 = kDAT_0035322c_deathNudge;
          entity.verticalVelocity44 = kDAT_0035322c_deathNudge;
        }
        else
        {
          FUN_00225bf0_set_state_and_animation(entity, 8, 0);
          entity.hitFlagsC2 = 0;
        }
        entity.pendingDamageBe = 0;
      }
    }

    const std::uint32_t handler = environment.dispatchTable->stateHandler(
        kPTR_FUN_00325970_enemy80States, kEnemy80StateCount, entity.state60);
    const bool implemented = entity.state60 == 0 || entity.state60 == 1;
    trace.recordStateDispatch(entity.typeId00, entity.state60, handler, implemented);
    if (entity.state60 == 0)
    {
      enemy_state0(entity, slot, environment);
    }
    else if (entity.state60 == 1 && haveRecord)
    {
      FUN_0027fa88_enemy80_turn(entity, environment, view);
    }

    FUN_00280850_wobble(entity, slot, environment);

    // :64-90. Three rolls against the record's current action byte, each keying
    // the same ambient cue at a different rate: one frame in eighty while it
    // holds (action 6), one in ten while it closes (2), one in twenty while it
    // strikes (4).
    if (haveRecord && environment.random && environment.FUN_00267d38_playSound)
    {
      const auto roll = [&environment]() -> std::int32_t
      { return static_cast<std::int16_t>(environment.random()); };
      if (view.currentAction0f == 6 && roll() % 0x50 == 1)
      {
        environment.FUN_00267d38_playSound(kFUN_0027f288_ambientCue, entity);
      }
      if (view.currentAction0f == 2 && roll() % 10 == 1)
      {
        environment.FUN_00267d38_playSound(kFUN_0027f288_ambientCue, entity);
      }
      if (view.currentAction0f == 4 && roll() % 0x14 == 1)
      {
        environment.FUN_00267d38_playSound(kFUN_0027f288_ambientCue, entity);
      }
    }

    publish();
  }

  void FUN_0028a958_enemy8a(OriginalEntity &entity,
                            std::size_t slot,
                            const ActorEnvironment &environment,
                            ActorTrace &trace)
  {
    if (environment.entityPool == nullptr || environment.dispatchTable == nullptr)
    {
      return;
    }

    // uGpffffb052 bit 3, the "battle is over" broadcast: drop whatever the
    // enemy was carrying and go to state 5. The +0x1A8 half of it -- the spit
    // projectile this type links to itself, torn down through FUN_002ec750 and
    // FUN_0028b740 -- cannot be reached in the port, because only that unported
    // attack writes the link.
    if ((environment.sGpffffb052_battleFlags & 8u) != 0 && entity.state60 != 5)
    {
      entity.fadeRamp62 = 0;
      FUN_00225bf0_set_state_and_animation(entity, 5, 8);
    }

    // The step. +0x06 bit 0 is "the animation came round", so this keys once a
    // loop rather than once a frame.
    if (entity.animationA0 == 1 && (entity.flags06 & 1u) != 0 && environment.FUN_00267d38_playSound)
    {
      environment.FUN_00267d38_playSound(kFUN_0028a958_stepCue, entity);
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

    if (!enemy_action_check(entity, environment, view, haveRecord, false))
    {
      if (FUN_0023a068_freeze_gate(entity, environment.frameTicks))
      {
        publish();
        return;
      }

      if (entity.pendingDamageBe != 0)
      {
        const std::int32_t remaining = static_cast<std::int32_t>(entity.staggerTimer12a) -
                                       static_cast<std::int32_t>(entity.pendingDamageBe);
        entity.staggerTimer12a = static_cast<std::uint16_t>(remaining);
        const std::uint32_t pick = environment.random ? environment.random() : 0;
        if (static_cast<std::int16_t>(entity.staggerTimer12a) < 1)
        {
          entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 0x10u);
          FUN_00225bf0_set_state_and_animation(entity, 5,
                                               static_cast<std::uint16_t>((pick & 1u) == 0 ? 9 : 8));
        }
        else
        {
          FUN_00225bf0_set_state_and_animation(
              entity, 6, static_cast<std::uint16_t>((pick & 1u) == 0 ? 0x10 : 0x0F));
        }
        entity.hitFlagsC2 = 0;
        entity.pendingDamageBe = 0;
      }
    }

    const std::uint32_t handler = environment.dispatchTable->stateHandler(
        kPTR_FUN_00325B40_enemy8aStates, kEnemy8aStateCount, entity.state60);
    const bool implemented = entity.state60 == 0 || entity.state60 == 1;
    trace.recordStateDispatch(entity.typeId00, entity.state60, handler, implemented);
    if (entity.state60 == 0)
    {
      enemy_state0(entity, slot, environment);
    }
    else if (entity.state60 == 1 && haveRecord)
    {
      FUN_0028af28_enemy8a_turn(entity, environment, view);
    }

    publish();
  }

} // namespace orphen::ported::entity
