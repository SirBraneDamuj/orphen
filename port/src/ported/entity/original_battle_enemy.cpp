#include "ported/entity/original_battle_enemy.h"

#include "ported/entity/original_enemy_attack.h"

#include "ported/entity/actor_dispatch_table.h"
#include "ported/model/psc3_skeleton.h"

#include <array>
#include <cmath>
#include <functional>

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
    // The attack states' own rates and cues. Every turn rate in both types is
    // the same 0.17453289 -- ten degrees a frame at 32 ticks -- and they are
    // kept apart anyway because they are four separate words in the ELF and a
    // future scene could prove one of them different.
    inline constexpr float kFGpffff92cc_enemy80LeapTurnRate = 0.17453289031982422f;
    inline constexpr float kFGpffff92d4_enemy80LungeTurnRate = 0.17453289031982422f;
    inline constexpr float kFGpffff95bc_enemy8aBiteTurnRate = 0.17453289031982422f;
    inline constexpr float kDAT_0035353c_enemy8aSpitTurnRate = 0.17453289031982422f;
    // fGpffff92d0 at 0x00353240: how far above the target the leap's arc peaks.
    inline constexpr float kFGpffff92d0_leapApex = 0.30000001192092896f;
    inline constexpr std::uint16_t kFUN_0027fdf8_lungeCue = 0x1E4;
    inline constexpr std::uint16_t kFUN_0028afc0_biteCue = 0x1C5;
    inline constexpr std::uint16_t kFUN_0028b420_spitCue = 0x1C9;
    // The damage reaction's own rates and cues. DAT_00353250 is the rate the
    // knocked-back 0x80 turns toward its spawn point at, DAT_00353254 the same
    // 0.001 nudge the death branch in the wrapper uses.
    inline constexpr float kDAT_00353250_enemy80RecoverTurnRate = 0.17453289031982422f;
    inline constexpr float kDAT_00353254_enemy80DeathNudge = 0.0010000000474974513f;
    inline constexpr std::uint16_t kFUN_00280628_enemy80HitCue = 0x1E5;
    inline constexpr std::uint16_t kFUN_00280560_enemy80DeathCue = 0x1E6;
    inline constexpr std::uint16_t kFUN_0028b698_enemy8aHitCue = 0x1CA;
    inline constexpr std::uint16_t kFUN_0028b568_enemy8aDeathCue = 0x1CB;

    // FUN_0028b0e8, the clone's own three constants: how fast it turns toward
    // what it is chasing (no 0.03125 on this one -- it is already per tick),
    // and the height band the grab has to land inside.
    inline constexpr float kDAT_00353530_enemy8aCloneTurnRate = 0.00436332216f;
    inline constexpr float kDAT_00353534_cloneGrabHeight = 0.300000012f;
    inline constexpr float kDAT_00353538_cloneGrabDepth = -0.300000012f;
    inline constexpr std::uint16_t kFUN_0028b0e8_cloneCue = 0x1C6;

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
    // The three FUN_00216078 calls are absent, and that is a *deferral*, not a
    // free omission. They copy a type's first three attack records into globals
    // at 0x005739B0 / 0x0058B140, and 0x005739B0 does have a reader:
    // FUN_00280698, the box FUN_0027fb30 sweeps through FUN_00215ac8 on every
    // frame of a leap. That sweep is not ported either -- it is one of the five
    // calls the damage front is waiting on, listed on the attack states below
    // -- so nothing here reads a record that was never filled. The only other
    // lasting effect is DAT_00354C64, the record count, which FUN_0023f8b8
    // re-establishes for itself on the next line.
    void enemy_state0(OriginalEntity &entity,
                      std::size_t slot,
                      const ActorEnvironment &environment,
                      bool isEnemy8a)
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

      // The three FUN_00216078 calls: this type's attack records 0, 1 and 2,
      // copied into the type's own global bank. Record 0 is what the flyer's
      // swoop and the clone's grab charge with, record 2 what the flyer's shot
      // and the Maneater's spit carry.
      FUN_00216078_fill_attack_records(
          static_cast<std::int16_t>(entity.typeId00),
          isEnemy8a ? DAT_0058b140_enemy8aAttacks() : DAT_005739b0_enemy80Attacks(), environment);

      // FUN_0023f8b8, from the caller the original really uses. The port also
      // calls it at the spawn, for the enemy types whose state 0 is not ported
      // yet; the second call is idempotent.
      if (environment.FUN_0023f8b8_bind_battle_actor)
      {
        entity.battleActorRecord198 = environment.FUN_0023f8b8_bind_battle_actor(slot);
      }

      // FUN_0028ae10's last line, and the 0x8A's only difference from the
      // flyer's state 0. +0x1AC is what FUN_0028b568 branches on when the
      // Maneater dies: 1 means "placed, and it owns a seed link"; a clone grown
      // by FUN_0028b740 carries 2 instead and tears down its *parent's* link.
      if (isEnemy8a)
      {
        entity.enemySpawnFlag1ac = 1;
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
    // Actions 1..8 are the battle script's vocabulary. The switch is on the
    // *pending* byte and only actions 1, 2, 4, 5, 6, 7 and 8 reach the type's
    // own action table; 0x0A parks the enemy and reports itself busy without
    // clearing the request, 0x0B only latches, and anything else is the
    // original's own diagnostic print. `dispatch` is the type's table --
    // FUN_0027f5c8 or FUN_0028ac40 -- because that is the only line where the
    // two wrappers differ.
    bool enemy_action_check(OriginalEntity &entity,
                            const ActorEnvironment &environment,
                            ActorEnvironment::BattleActorView &view,
                            bool haveRecord,
                            bool useRecordTarget,
                            const std::function<void(std::int16_t action)> &dispatch)
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
        // The action body publishes +0x0F itself -- each arm of the table sets
        // a different one, and action 4 on type 0x8A deliberately publishes 2.
        // The pending byte is cleared after it runs, not before.
        if (action == 1 || action == 2 || action == 4 || action == 5 || action == 6 ||
            action == 7 || action == 8)
        {
          dispatch(static_cast<std::int16_t>(action));
        }
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
    // ---------------------------------------------------------- the actions
    //
    // FUN_0023a6d0(reach, entity, targetPosition): how many ticks the enemy
    // should be given to cover the gap, as the plain 2-D distance divided by
    // reach/1000, armed by the same `(n << 21) >> 16` -- n truncated to eleven
    // bits, times 32 -- that every other battle timer uses.
    std::int16_t FUN_0023a6d0_travel_ticks(float reach,
                                           const OriginalEntity &entity,
                                           float targetX,
                                           float targetZ)
    {
      const float dx = targetX - entity.positionX20;
      const float dz = targetZ - entity.positionZ24;
      const float distance = std::sqrt(dx * dx + dz * dz);
      const std::int32_t ticks = static_cast<std::int32_t>(distance / (reach / 1000.0f));
      return static_cast<std::int16_t>((ticks << 21) >> 16);
    }

    // The original takes a bare float pair, so the call sites that hand it a
    // record's spawn triple and the ones that hand it an entity are the same
    // function; this overload is the entity spelling.
    std::int16_t FUN_0023a6d0_travel_ticks(float reach,
                                           const OriginalEntity &entity,
                                           const OriginalEntity &target)
    {
      return FUN_0023a6d0_travel_ticks(reach, entity, target.positionX20, target.positionZ24);
    }

    // FUN_0027f5c8: **type 0x80's action table.** The wrapper has already
    // latched the busy bit; this is what an order actually means.
    //
    //   1  stand still: state 0
    //   2  close -- aim at the target, reach 40, state 2 (the leap)
    //   4  strike -- the same, reach 60, state 3 (the lunge)
    //   5  roll a coin and become 2 or 4
    //   6  the idle default, inline: re-aim, roll a 100..199-tick hold, state 1
    //   7  back off: turn 0..44 degrees off the bearing *away* from the target
    //      and walk, unless something is already pushing the entity, in which
    //      case follow the push instead
    //   8  FUN_00280728, the type's own retreat, not on any script's path here
    //
    // The record's +0x1A -- spelled `+0x198 + 0x0E`, so biased like everything
    // else off +0x198 -- is added to the reach, so a record can make its actor
    // keep its distance without changing the type.
    void FUN_0027f5c8_enemy80_action(OriginalEntity &entity,
                                     const ActorEnvironment &environment,
                                     ActorEnvironment::BattleActorView &view,
                                     std::int16_t action)
    {
      const EntityPool &pool = *environment.entityPool;
      const auto random = [&environment]() -> std::uint32_t
      { return environment.random ? environment.random() : 0u; };

      if (action == 1)
      {
        FUN_00225bf0_set_state_and_animation(entity, 0, 0);
        view.currentAction0f = 1;
        return;
      }

      if (action == 6)
      {
        const OriginalEntity &target = FUN_0023a958_target(pool, view.target2c);
        entity.battleDesiredFacing19c = FUN_0023a4b8_bearing(entity, target);
        entity.fadeRamp62 = static_cast<std::uint16_t>(
            (static_cast<std::int16_t>(static_cast<std::int32_t>(random()) % 100) + 100) * 0x20);
        FUN_00225bf0_set_state_and_animation(entity, 1,
                                             static_cast<std::uint16_t>((random() & 1u) == 0 ? 1 : 0));
        view.currentAction0f = 6;
        return;
      }

      if (action == 5)
      {
        action = (random() & 1u) == 0 ? 2 : 4;
      }

      if (action == 2 || action == 4)
      {
        const std::int16_t target = view.target2c;
        const OriginalEntity &targetEntity = FUN_0023a958_target(pool, target);
        entity.enemyTargetSlot1a4 = static_cast<std::int32_t>(
            (target < 0 || static_cast<std::size_t>(target) >= pool.slotCount()) ? 0 : target);
        entity.battleDesiredFacing19c = FUN_0023a4b8_bearing(entity, targetEntity);
        entity.enemyReach1a0 = action == 2 ? 40.0f : 60.0f;
        entity.fadeRamp62 = static_cast<std::uint16_t>(FUN_0023a6d0_travel_ticks(
            static_cast<float>(view.attackRange1a) + entity.enemyReach1a0, entity, targetEntity));
        if (action == 2)
        {
          FUN_00225bf0_set_state_and_animation(entity, 2, 0);
          FUN_00215e48_clear_hit_set(entity);
        }
        else
        {
          FUN_00225bf0_set_state_and_animation(entity, 3, 0);
        }
        view.currentAction0f = static_cast<std::uint8_t>(action);
        return;
      }

      if (action == 7)
      {
        entity.enemyReach1a0 = 30.0f;
        if (entity.velocityX3c == 0.0f && entity.velocityZ40 == 0.0f)
        {
          // Nothing is pushing it: pick a bearing 0..44 degrees either side of
          // the one *away* from the player. FUN_0023a480 is the no-record
          // bearing -- it reads DAT_0058BED0/D4, which is pool slot 0's own
          // +0x20/+0x24, with no record consulted at all.
          const float away = FUN_0023a4b8_bearing(entity, pool.slot(0));
          const float spread = (static_cast<float>(static_cast<std::uint32_t>(random()) % 0x2Du) *
                                kFGpffff92e8_tau) /
                               360.0f;
          entity.battleDesiredFacing19c = (random() & 1u) == 0 ? away - spread : away + spread;
          entity.fadeRamp62 = static_cast<std::uint16_t>(
              (static_cast<std::int16_t>(static_cast<std::int32_t>(random()) % 100) + 100) * 0x20);
        }
        else
        {
          // Verbatim, and it does read oddly: FUN_00305408 is handed
          // (+0x40 - +0x24) and (+0x3C - +0x20), a *velocity* minus a
          // *position*. Reproduced as written rather than corrected into
          // atan2(velocityZ, velocityX) -- there is no evidence for the second
          // reading, and this branch only runs while something else is already
          // pushing the entity.
          entity.battleDesiredFacing19c =
              std::atan2(entity.velocityZ40 - entity.positionZ24,
                         entity.velocityX3c - entity.positionX20);
        }
        FUN_00225bf0_set_state_and_animation(entity, 4,
                                             static_cast<std::uint16_t>((random() & 1u) == 0 ? 3 : 2));
        view.currentAction0f = 7;
        return;
      }

      // Action 8 is FUN_00280728, which is not on any script's path in the
      // scenes the port loads. Left to the state trace rather than guessed at.
    }

    // FUN_0028ac40: **type 0x8A's action table**, and a much shorter one,
    // because a Maneater is rooted -- it never walks at anything.
    //
    //   1  state 0
    //   2  bite: aim, state 2, animation 4 or 5 -- but only when neither of the
    //      two attack links at +0x1A4 and +0x1A8 is still in flight
    //   4  spit: aim, 100 ticks, state 4, animation 1, and the current action
    //      byte goes to **2**, not 4 -- so the gate that waits for the strike
    //      to finish cannot tell the two attacks apart, which is deliberate
    //   5  roll a coin and become 2 or 4
    //   6  the idle default, inline
    void FUN_0028ac40_enemy8a_action(OriginalEntity &entity,
                                     const ActorEnvironment &environment,
                                     ActorEnvironment::BattleActorView &view,
                                     std::int16_t action)
    {
      const EntityPool &pool = *environment.entityPool;
      const auto random = [&environment]() -> std::uint32_t
      { return environment.random ? environment.random() : 0u; };

      if (action == 1)
      {
        FUN_00225bf0_set_state_and_animation(entity, 0, 0);
        view.currentAction0f = 1;
        return;
      }

      if (action == 6)
      {
        entity.battleDesiredFacing19c = FUN_0023a4b8_bearing(entity, pool.slot(0));
        entity.fadeRamp62 = static_cast<std::uint16_t>(
            (static_cast<std::int16_t>(static_cast<std::int32_t>(random()) % 100) + 100) * 0x20);
        const std::uint32_t bits = random() & 3u;
        FUN_00225bf0_set_state_and_animation(
            entity, 1, static_cast<std::uint16_t>(bits == 0 ? 0 : (bits == 1 ? 2 : 3)));
        view.currentAction0f = 6;
        return;
      }

      if (action == 5)
      {
        action = (random() & 1u) == 0 ? 2 : 4;
      }

      if (action == 2)
      {
        if (entity.enemyAttackLink1a8 < 0 && entity.enemyAttackLink1a4 < 0)
        {
          entity.enemyTargetSlot1a0 = view.target2c < 0 ? 0 : view.target2c;
          entity.battleDesiredFacing19c =
              FUN_0023a4b8_bearing(entity, FUN_0023a958_target(pool, view.target2c));
          FUN_00225bf0_set_state_and_animation(
              entity, 2, static_cast<std::uint16_t>((random() & 1u) == 0 ? 5 : 4));
          view.currentAction0f = 2;
        }
        // Otherwise the order is simply dropped: the wrapper has already
        // cleared the pending byte, so the script's gate goes on waiting for
        // the attack still in the air.
        return;
      }

      if (action == 4)
      {
        entity.enemyTargetSlot1a0 = view.target2c < 0 ? 0 : view.target2c;
        entity.battleDesiredFacing19c =
            FUN_0023a4b8_bearing(entity, FUN_0023a958_target(pool, view.target2c));
        entity.fadeRamp62 = 0xC80; // 100 ticks
        FUN_00225bf0_set_state_and_animation(entity, 4, 1);
        view.currentAction0f = 2;
      }
    }

    // --------------------------------------------------------- attack states

    // FUN_0027fb30, type 0x80 state 2: **the leap.** Turn to the bearing first
    // and do nothing else until the turn is finished, then, once per animation:
    //
    //   anim 0  build the arc. The start is where it stands; the end is two
    //           units past the target along its own facing, at the target's
    //           height plus 0.3; the middle control point of X and Z is the
    //           start, so the curve leaves along the ground and arrives on the
    //           target. Animation 3, +0x04 bit 0 raised (physics off), and the
    //           progress accumulator zeroed.
    //   anim 3  walk the arc, sweeping the position by the *difference* rather
    //           than assigning it, so the physics delta is what moves the
    //           entity. FUN_002ebde0 fires on the animation's own hit frame.
    //           Past the end -- or the moment the entity touches anything, the
    //           0x4066 mask on +0x0C -- animation 2 and 50 ticks of falling.
    //   anim 2  drop 30 units per 32000 ticks until the timer runs out, then
    //           state 6, which walks it home.
    void FUN_0027fb30_enemy80_leap(OriginalEntity &entity,
                                   std::size_t slot,
                                   const ActorEnvironment &environment,
                                   ActorEnvironment::BattleActorView &view,
                                   ActorTrace &trace)
    {
      const EntityPool &pool = *environment.entityPool;
      view.flags38 |= 1u;

      const float delta = FUN_0023a320_approach_angle(
          entity.facingRadians5c, entity.battleDesiredFacing19c,
          static_cast<float>(environment.frameTicks) * kFGpffff92cc_enemy80LeapTurnRate * 0.03125f);
      if (delta != 0.0f)
      {
        entity.facingRadians5c += delta;
        return;
      }

      if (entity.animationA0 == 0)
      {
        const std::size_t targetSlot = static_cast<std::size_t>(
            entity.enemyTargetSlot1a4 < 0 ? 0 : entity.enemyTargetSlot1a4);
        const OriginalEntity &target = pool.slot(targetSlot);

        entity.enemyArcX1a8[0] = entity.positionX20;
        entity.enemyArcZ1b4[0] = entity.positionZ24;
        entity.enemyArcY1c0[0] = entity.positionY28;

        const float reach = 2.0f;
        entity.enemyArcX1a8[2] =
            target.positionX20 + std::cos(entity.facingRadians5c) * reach;
        entity.enemyArcZ1b4[2] =
            target.positionZ24 + std::sin(entity.facingRadians5c) * reach;
        const float apex = target.positionY28 + kFGpffff92d0_leapApex;

        entity.enemyArcX1a8[1] = entity.enemyArcX1a8[0];
        entity.enemyArcZ1b4[1] = entity.enemyArcZ1b4[0];
        entity.enemyArcY1c0[1] = apex;
        entity.enemyArcY1c0[2] = apex;

        FUN_00225bc8_set_animation(entity, 3);
        entity.enemyArcProgress1cc = 0.0f;
        entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 1u);
        return;
      }

      if (entity.animationA0 == 3)
      {
        const float total = static_cast<float>(static_cast<std::int16_t>(entity.fadeRamp62));
        const float t = total != 0.0f ? entity.enemyArcProgress1cc / total : 1.0f;
        if (t < 1.0f && (entity.collisionFlags0c & 0x4066u) == 0)
        {
          // FUN_00280698: a box the size of the enemy, swept through
          // FUN_00215ac8 against the type's attack record 0, every frame of
          // the arc. This is the swoop's whole damage -- there is no separate
          // hit volume entity, the flyer's own body is it.
          FUN_00280698_swoop_hit_test(entity, slot, environment);
          entity.desiredDeltaX30 +=
              FUN_0023a990_bezier(t, entity.enemyArcX1a8) - entity.positionX20;
          entity.desiredDeltaZ34 +=
              FUN_0023a990_bezier(t, entity.enemyArcZ1b4) - entity.positionZ24;
          entity.desiredDeltaY38 +=
              FUN_0023a990_bezier(t, entity.enemyArcY1c0) - entity.positionY28;
          entity.enemyArcProgress1cc += static_cast<float>(environment.frameTicks);
          if ((entity.flags06 & 4u) != 0)
          {
            // FUN_002ebde0(entity, 6): six puffs of dust, and nothing else --
            // the ring is decoration for the sweep above, not a second hit.
            FUN_002ebde0_spawn_swoop_ring(entity, slot, 6, environment);
            trace.recordEnemyAttackHit();
          }
          return;
        }
        entity.fadeRamp62 = 0x640; // 50 ticks
        entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 & 0xFFFEu);
        FUN_00225bc8_set_animation(entity, 2);
        return;
      }

      if (entity.animationA0 == 2)
      {
        entity.fadeRamp62 = static_cast<std::uint16_t>(
            FUN_0023a678_countdown(static_cast<std::int16_t>(entity.fadeRamp62),
                                   environment.frameTicks));
        if (entity.fadeRamp62 == 0)
        {
          FUN_00225bf0_set_state_and_animation(entity, 6, 0);
          return;
        }
        const float drop =
            entity.positionY28 + (static_cast<float>(environment.frameTicks) * 30.0f) / 32000.0f;
        entity.groundHeight4c = drop;
        entity.positionY28 = drop;
      }
    }

    // FUN_0027fdf8, type 0x80 state 3: **the lunge.** The same shape as the
    // leap and three differences that matter: the arc's end is computed from
    // the bearing *from the target back to the enemy* rather than from the
    // enemy's own facing, so it stops short instead of overshooting; the apex
    // is a flat 1.5 above the target; and the hit is FUN_002ebad8 on the
    // animation's own frame, followed by cue 0x1E4, rather than a swept volume.
    void FUN_0027fdf8_enemy80_lunge(OriginalEntity &entity,
                                    std::size_t slot,
                                    const ActorEnvironment &environment,
                                    ActorEnvironment::BattleActorView &view,
                                    ActorTrace &trace)
    {
      const EntityPool &pool = *environment.entityPool;
      view.flags38 |= 1u;

      const float delta = FUN_0023a320_approach_angle(
          entity.facingRadians5c, entity.battleDesiredFacing19c,
          static_cast<float>(environment.frameTicks) * kFGpffff92d4_enemy80LungeTurnRate * 0.03125f);
      if (delta != 0.0f)
      {
        entity.facingRadians5c += delta;
      }

      const std::size_t targetSlot =
          static_cast<std::size_t>(entity.enemyTargetSlot1a4 < 0 ? 0 : entity.enemyTargetSlot1a4);
      const OriginalEntity &target = pool.slot(targetSlot);

      if (entity.animationA0 == 0)
      {
        entity.enemyArcX1a8[0] = entity.positionX20;
        entity.enemyArcZ1b4[0] = entity.positionZ24;
        entity.enemyArcY1c0[0] = entity.positionY28;

        const float back = std::atan2(entity.positionZ24 - target.positionZ24,
                                      entity.positionX20 - target.positionX20);
        entity.enemyArcX1a8[2] = target.positionX20 + std::cos(back) * 2.0f;
        entity.enemyArcZ1b4[2] = target.positionZ24 + std::sin(back) * 2.0f;

        entity.enemyArcProgress1cc = 0.0f;
        entity.enemyArcX1a8[1] = entity.enemyArcX1a8[2];
        entity.enemyArcZ1b4[1] = entity.enemyArcZ1b4[2];
        entity.enemyArcY1c0[1] = entity.enemyArcY1c0[0];
        entity.enemyArcY1c0[2] = target.positionY28 + 1.5f;
        FUN_00225bc8_set_animation(entity, 2);
        return;
      }

      if (entity.animationA0 == 2)
      {
        const float total = static_cast<float>(static_cast<std::int16_t>(entity.fadeRamp62));
        const float t = total != 0.0f ? entity.enemyArcProgress1cc / total : 1.0f;
        if (t < 1.0f)
        {
          entity.desiredDeltaX30 +=
              FUN_0023a990_bezier(t, entity.enemyArcX1a8) - entity.positionX20;
          entity.desiredDeltaZ34 +=
              FUN_0023a990_bezier(t, entity.enemyArcZ1b4) - entity.positionZ24;
          entity.desiredDeltaY38 +=
              FUN_0023a990_bezier(t, entity.enemyArcY1c0) - entity.positionY28;
        }
        else
        {
          FUN_00225bc8_set_animation(entity, 7);
        }
        entity.enemyArcProgress1cc += static_cast<float>(environment.frameTicks);
        return;
      }

      if (entity.animationA0 == 7)
      {
        if ((entity.flags06 & 1u) != 0)
        {
          // FUN_002ebad8(entity, target, 0x5739B8): **the shot.** The lunge is
          // not a ram at all -- the arc stops short of the target and the
          // animation's last frame launches a type 0x10E projectile from bone
          // 11, carrying the type's attack record 2. Without this the flyer
          // flew the arc, played the cue and fired nothing.
          FUN_002ebad8_spawn_shot(entity, slot, target,
                                  DAT_005739b0_enemy80Attacks().record[2], environment);
          trace.recordEnemyAttackHit();
          if (environment.FUN_00267d38_playSound)
          {
            environment.FUN_00267d38_playSound(kFUN_0027fdf8_lungeCue, entity);
          }
          FUN_00225bc8_set_animation(entity, 3);
          entity.fadeRamp62 = 0x640;
        }
        return;
      }

      if (entity.animationA0 == 3)
      {
        entity.fadeRamp62 = static_cast<std::uint16_t>(
            FUN_0023a678_countdown(static_cast<std::int16_t>(entity.fadeRamp62),
                                   environment.frameTicks));
        if (entity.fadeRamp62 == 0)
        {
          FUN_00225bf0_set_state_and_animation(entity, 6, 0);
          return;
        }
        const float drop =
            entity.positionY28 + (static_cast<float>(environment.frameTicks) * 30.0f) / 32000.0f;
        entity.groundHeight4c = drop;
        entity.positionY28 = drop;
      }
    }

    // FUN_00280428, type 0x80 state 6: **going home.** The record's spawn
    // position -- +0x14/+0x16/+0x18, world times ten, reached as +0x198 + 8
    // through the same 0x0C bias -- is where it teleports to, two units up,
    // and it then sinks back down at the same 30-per-32000 the leap fell at.
    // The busy bit is cleared the frame it lands, which is what finally lets
    // the idle default stamp the current action back to 6 and release the AI
    // script's gate.
    void FUN_00280428_enemy80_return(OriginalEntity &entity,
                                     const ActorEnvironment &environment,
                                     ActorEnvironment::BattleActorView &view)
    {
      view.flags38 |= 1u;
      if (entity.animationA0 == 0)
      {
        view.currentAction0f = 8;
        entity.positionX20 = static_cast<float>(view.spawnX14) / 10.0f;
        entity.positionZ24 = static_cast<float>(view.spawnZ16) / 10.0f;
        const float up = static_cast<float>(view.spawnY18) / 10.0f + 2.0f;
        entity.groundHeight4c = up;
        entity.positionY28 = up;
        FUN_00225bc8_set_animation(entity, 2);
        return;
      }

      const float floor = static_cast<float>(view.spawnY18) / 10.0f;
      if (entity.positionY28 <= floor)
      {
        entity.positionY28 = floor;
        view.flags38 &= ~1u;
        return;
      }
      const float drop =
          entity.positionY28 - (static_cast<float>(environment.frameTicks) * 30.0f) / 32000.0f;
      entity.groundHeight4c = drop;
      entity.positionY28 = drop;
    }

    // FUN_0028afc0, type 0x8A state 2: **the bite.** It does not move at all --
    // it turns while animations 4 and 5 run, spawns the bite volume on timeline
    // cursor 10, and drops to animation 1 when the clip comes round. Animation
    // 1 with nothing in flight is what clears the busy bit.
    void FUN_0028afc0_enemy8a_bite(OriginalEntity &entity,
                                   std::size_t slot,
                                   const ActorEnvironment &environment,
                                   ActorEnvironment::BattleActorView &view,
                                   ActorTrace &trace)
    {
      view.flags38 |= 1u;
      const std::uint16_t animation = entity.animationA0;

      if (static_cast<std::uint16_t>(animation - 4) < 2)
      {
        const float delta = FUN_0023a320_approach_angle(
            entity.facingRadians5c, entity.battleDesiredFacing19c,
            static_cast<float>(environment.frameTicks) * kFGpffff95bc_enemy8aBiteTurnRate *
                0.03125f);
        if (delta != 0.0f)
        {
          entity.facingRadians5c += delta;
        }
        if (entity.timelineCursorA8 == 10 && (entity.flags06 & 4u) != 0)
        {
          // FUN_002ec920(entity, target, 13): **the seed.** State 2 is not a
          // bite that reaches anybody -- it lobs a type 0x112 off bone 13 at a
          // point just in front of the target, and hangs it off +0x1A8. The
          // wrapper walks that link every frame; when it lands, FUN_0028b740
          // grows a second Maneater out of it.
          entity.enemyAttackLink1a8 = FUN_002ec920_spawn_seed(
              entity, slot,
              FUN_0023a958_target(*environment.entityPool,
                                  static_cast<std::int16_t>(entity.enemyTargetSlot1a0)),
              13, environment);
          trace.recordEnemyAttackHit();
          if (environment.FUN_00267d38_playSound)
          {
            environment.FUN_00267d38_playSound(kFUN_0028afc0_biteCue, entity);
          }
        }
        if ((entity.flags06 & 1u) != 0)
        {
          FUN_00225bc8_set_animation(entity, 1);
        }
        return;
      }

      if (animation == 1 && entity.enemyAttackLink1a4 < 0 && entity.enemyAttackLink1a8 < 0)
      {
        view.flags38 &= ~1u;
      }
    }

    // FUN_0028b420, type 0x8A state 4: **the spit.** Animation 1 is the wind-up
    // -- turn while +0x62 runs down -- and animation 0x0E is the throw, whose
    // last frame clears the busy bit and hands the projectile to
    // FUN_00216128's pool at DAT_0058B148.
    void FUN_0028b420_enemy8a_spit(OriginalEntity &entity,
                                   std::size_t slot,
                                   const ActorEnvironment &environment,
                                   ActorEnvironment::BattleActorView &view,
                                   ActorTrace &trace)
    {
      view.flags38 |= 1u;
      entity.fadeRamp62 = static_cast<std::uint16_t>(FUN_0023a678_countdown(
          static_cast<std::int16_t>(entity.fadeRamp62), environment.frameTicks));

      if (entity.animationA0 == 1)
      {
        if (entity.fadeRamp62 == 0)
        {
          FUN_00225bc8_set_animation(entity, 0x0E);
          return;
        }
        const float delta = FUN_0023a320_approach_angle(
            entity.facingRadians5c, entity.battleDesiredFacing19c,
            static_cast<float>(environment.frameTicks) * kDAT_0035353c_enemy8aSpitTurnRate *
                0.03125f);
        if (delta != 0.0f)
        {
          entity.facingRadians5c += delta;
        }
        return;
      }

      if (entity.animationA0 == 0x0E)
      {
        if ((entity.timelineCursorA8 & 1u) == 0 &&
            static_cast<std::int16_t>(entity.timelineCursorA8) < 10 &&
            (entity.flags06 & 4u) != 0)
        {
          // FUN_002ecc68(entity): **the spores.** Eight type 0x113 orbs off
          // bone 13, spread evenly and drifting outward as they swell. They
          // carry no hit test of their own -- the damage is the direct charge
          // below.
          FUN_002ecc68_spawn_spores(entity, slot, environment);
          trace.recordEnemyAttackHit();
          if (environment.FUN_00267d38_playSound)
          {
            environment.FUN_00267d38_playSound(kFUN_0028b420_spitCue, entity);
          }
        }
        if ((entity.flags06 & 1u) != 0)
        {
          view.flags38 &= ~1u;
          // FUN_00216128(0x58B148, entity, target): the spit's damage, charged
          // straight to the target with no test at all. The orbs above are the
          // picture of it; this is the hit.
          const std::size_t targetSlot = static_cast<std::size_t>(
              entity.enemyTargetSlot1a0 < 0 ? 0 : entity.enemyTargetSlot1a0);
          if (targetSlot < environment.entityPool->slotCount())
          {
            FUN_00216128_direct_hit(entity, slot, environment.entityPool->slot(targetSlot),
                                    DAT_0058b140_enemy8aAttacks().record[2], environment);
          }
        }
      }
    }

    // FUN_0028b0e8, type 0x8A state 3: **the clone's bite.** Nothing placed by
    // the scene ever enters this state -- it belongs to the Maneater that grew
    // out of a seed, and FUN_0028b740 drops it straight in. A clone has one hit
    // point, no actor record and no AI script; the state is its whole life.
    //
    //   FUN_0023eff8 == 0   the fight is over, so there is nobody to bite.
    //   anim 10 / 11        walk in, turning toward the target every frame.
    //   anim 12             the lunge. Its last frame decides: within one unit,
    //                       within 0.3 in height, the target on the ground, not
    //                       already held by something and not somebody's child
    //                       -- then the grab. Anything else and it misses.
    //   anim 13             the hold, 0x0C80 ticks, and FUN_00216128 charges
    //                       attack record 0 when it ends.
    //   anything else       run the timer out and die.
    //
    // Either way it ends in state 5. The grab is the only place in either enemy
    // that writes the *victim's* +0x96 bit 2, which is what pins the player in
    // place while the clone chews.
    void FUN_0028b0e8_enemy8a_clone(OriginalEntity &entity,
                                    std::size_t slot,
                                    const ActorEnvironment &environment)
    {
      EntityPool &pool = *environment.entityPool;
      const auto random = [&environment]() -> std::uint32_t
      { return environment.random ? environment.random() : 0u; };
      const auto die = [&]()
      {
        FUN_00225bf0_set_state_and_animation(
            entity, 5, static_cast<std::uint16_t>((random() & 1u) != 0 ? 8 : 9));
      };

      if (environment.FUN_0023eff8_enemy_count && environment.FUN_0023eff8_enemy_count() == 0)
      {
        FUN_00225bf0_set_state_and_animation(entity, 5, 8);
        return;
      }

      const std::size_t targetSlot = static_cast<std::size_t>(
          entity.enemyTargetSlot1a0 < 0 ? 0 : entity.enemyTargetSlot1a0);
      OriginalEntity &target = pool.slot(targetSlot);

      const float bearing = FUN_0023a4b8_bearing(entity, target);
      const float delta = FUN_0023a320_approach_angle(
          entity.facingRadians5c, bearing,
          static_cast<float>(environment.frameTicks) * kDAT_00353530_enemy8aCloneTurnRate);
      if (delta == 0.0f)
      {
        entity.facingRadians5c = bearing;
      }
      else
      {
        entity.facingRadians5c += delta;
      }

      const std::uint16_t animation = entity.animationA0;
      if (static_cast<std::uint16_t>(animation - 10) < 2)
      {
        if (entity.timelineCursorA8 == 0 && (entity.flags06 & 4u) != 0 &&
            environment.FUN_00267d38_playSound)
        {
          environment.FUN_00267d38_playSound(kFUN_0028b0e8_cloneCue, entity);
        }
        if ((entity.flags06 & 1u) != 0)
        {
          FUN_00225bc8_set_animation(entity, 12);
        }
        return;
      }

      if (animation == 12)
      {
        if ((entity.flags06 & 4u) != 0 && environment.FUN_00267d38_playSound)
        {
          environment.FUN_00267d38_playSound(kFUN_0028b0e8_cloneCue, entity);
        }
        if ((entity.flags06 & 1u) == 0)
        {
          return;
        }
        const float dx = entity.positionX20 - target.positionX20;
        const float dz = entity.positionZ24 - target.positionZ24;
        const float dy = entity.positionY28 - target.positionY28;
        const bool missed = std::sqrt(dx * dx + dz * dz + dy * dy) > 1.0f ||
                            dy > kDAT_00353534_cloneGrabHeight ||
                            dy < kDAT_00353538_cloneGrabDepth ||
                            (target.collisionFlags0c & 1u) == 0 || target.parentSlot192 != -1 ||
                            (target.battleFlags96 & 4u) != 0;
        if (missed)
        {
          FUN_00225bc8_set_animation(entity, 2);
          return;
        }
        target.battleFlags96 = static_cast<std::uint8_t>(target.battleFlags96 | 4u);
        entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 0x10u);
        FUN_00225bc8_set_animation(entity, 13);
        entity.fadeRamp62 = 0x0C80;
        return;
      }

      entity.fadeRamp62 = static_cast<std::uint16_t>(FUN_0023a678_countdown(
          static_cast<std::int16_t>(entity.fadeRamp62), environment.frameTicks));
      if (entity.fadeRamp62 != 0)
      {
        return;
      }
      if (animation == 13)
      {
        FUN_00216128_direct_hit(entity, slot, target, DAT_0058b140_enemy8aAttacks().record[0],
                                environment);
        target.battleFlags96 = static_cast<std::uint8_t>(target.battleFlags96 & 0xFBu);
        entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 & 0xFFEFu);
      }
      die();
    }

    // ------------------------------------------------- taking a hit and dying
    //
    // FUN_00280728, reached from type 0x80's state 8 and from its action 8:
    // **the knock-back flight home.** It publishes current action 8 -- so the
    // AI script's gate keeps waiting -- and lays a quadratic Bezier from where
    // the hit left it back to the record's spawn point, flat in Y apart from
    // the two control points sitting at the current height. The reach is a flat
    // 50 plus the record's +0x1A, and the travel time is costed the same way
    // every other arc is.
    void FUN_00280728_enemy80_recover(OriginalEntity &entity,
                                      ActorEnvironment::BattleActorView &view)
    {
      view.currentAction0f = 8;
      entity.enemyReach1a0 = 50.0f;

      const float homeX = static_cast<float>(view.spawnX14) / 10.0f;
      const float homeZ = static_cast<float>(view.spawnZ16) / 10.0f;
      const float homeY = static_cast<float>(view.spawnY18) / 10.0f;

      entity.battleDesiredFacing19c =
          std::atan2(homeZ - entity.positionZ24, homeX - entity.positionX20);

      entity.enemyArcX1a8 = {{entity.positionX20, homeX, homeX}};
      entity.enemyArcZ1b4 = {{entity.positionZ24, homeZ, homeZ}};
      entity.enemyArcY1c0 = {{entity.positionY28, entity.positionY28, homeY}};
      entity.enemyArcProgress1cc = 0.0f;
      entity.fadeRamp62 = static_cast<std::uint16_t>(FUN_0023a6d0_travel_ticks(
          entity.enemyReach1a0 + static_cast<float>(view.attackRange1a), entity, homeX, homeZ));
      FUN_00225bf0_set_state_and_animation(entity, 5, 2);
    }

    // FUN_00280628, type 0x80 state 8: **the stagger.** The wrapper drops the
    // enemy in here the frame a hit lands that it survives. It holds the busy
    // bit -- so no order is taken while it reels -- keys the hit cue on
    // timeline cursor 4, and hands over to the flight home when the clip ends.
    void FUN_00280628_enemy80_hit(OriginalEntity &entity,
                                  const ActorEnvironment &environment,
                                  ActorEnvironment::BattleActorView &view)
    {
      view.flags38 |= 1u;
      if (entity.timelineCursorA8 == 4 && (entity.flags06 & 4u) != 0 &&
          environment.FUN_00267d38_playSound)
      {
        environment.FUN_00267d38_playSound(kFUN_00280628_enemy80HitCue, entity);
      }
      if ((entity.flags06 & 1u) != 0)
      {
        FUN_00280728_enemy80_recover(entity, view);
      }
    }

    // FUN_00280288, type 0x80 state 5: **the flight home.** The same shape as
    // the leap -- turn first, and only once the facing has arrived walk the
    // arc -- but the arc ends at the spawn point rather than at the target, and
    // the busy bit is released on the frame the walk runs out. Releasing it is
    // what lets the idle default stamp the current action back to 6 and free
    // the AI script's gate; without this state a staggered enemy holds that
    // gate for the rest of the fight.
    void FUN_00280288_enemy80_recover_flight(OriginalEntity &entity,
                                             const ActorEnvironment &environment,
                                             ActorEnvironment::BattleActorView &view)
    {
      view.flags38 |= 1u;
      const float delta = FUN_0023a320_approach_angle(
          entity.facingRadians5c, entity.battleDesiredFacing19c,
          static_cast<float>(environment.frameTicks) * kDAT_00353250_enemy80RecoverTurnRate *
              0.03125f);
      if (delta != 0.0f)
      {
        entity.facingRadians5c += delta;
        return;
      }

      const float t = entity.enemyArcProgress1cc /
                      static_cast<float>(static_cast<std::int16_t>(entity.fadeRamp62));
      if (t < 1.0f)
      {
        entity.desiredDeltaX30 += FUN_0023a990_bezier(t, entity.enemyArcX1a8) - entity.positionX20;
        entity.desiredDeltaZ34 += FUN_0023a990_bezier(t, entity.enemyArcZ1b4) - entity.positionZ24;
        entity.desiredDeltaY38 += FUN_0023a990_bezier(t, entity.enemyArcY1c0) - entity.positionY28;
      }
      else
      {
        view.flags38 &= ~1u;
        entity.positionX20 = static_cast<float>(view.spawnX14) / 10.0f;
        entity.positionZ24 = static_cast<float>(view.spawnZ16) / 10.0f;
        entity.positionY28 = static_cast<float>(view.spawnY18) / 10.0f;
      }
      entity.enemyArcProgress1cc += static_cast<float>(environment.frameTicks);
    }

    // FUN_00280560, type 0x80 state 7: **the death.** Two shapes, chosen by
    // which clip the wrapper's kill branch rolled. Cursor 6 is the one that
    // keys the death cue and gives the corpse the 0.001 nudge; every other clip
    // just latches +0x06 bit 0x10 on the frame it ends and raises +0x04 bit 0
    // -- the fade-and-free bit -- together with bit 0x800. Nothing in the
    // executable reads 0x800 back; it is set here and reproduced as written.
    void FUN_00280560_enemy80_death(OriginalEntity &entity,
                                    const ActorEnvironment &environment,
                                    ActorEnvironment::BattleActorView &view)
    {
      view.flags38 |= 1u;
      const std::uint16_t flags = entity.flags06;
      if (entity.timelineCursorA8 == 6)
      {
        if ((flags & 4u) != 0)
        {
          if ((flags & 0x10u) == 0 && environment.FUN_00267d38_playSound)
          {
            environment.FUN_00267d38_playSound(kFUN_00280560_enemy80DeathCue, entity);
          }
          entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10u);
          entity.desiredDeltaX30 = kDAT_00353254_enemy80DeathNudge;
        }
        if ((entity.collisionFlags0c & 1u) != 0 && (entity.flags06 & 0x10u) != 0)
        {
          entity.flags06 = static_cast<std::uint16_t>(entity.flags06 & 0xFFEFu);
        }
        return;
      }
      if ((flags & 1u) != 0 && (flags & 0x10u) == 0)
      {
        entity.flags06 = static_cast<std::uint16_t>(flags | 0x10u);
        entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 0x0801u);
      }
    }

    // FUN_0028b698, type 0x8A state 6: **the stagger**, and a much shorter one
    // than the flyer's, because a Maneater has nowhere to be knocked back to.
    // Hold the busy bit, key the hit cue on cursor 2, and on the frame the clip
    // ends release the bit and go straight back to the idle turn.
    void FUN_0028b698_enemy8a_hit(OriginalEntity &entity,
                                  const ActorEnvironment &environment,
                                  ActorEnvironment::BattleActorView &view,
                                  bool haveRecord)
    {
      if (haveRecord)
      {
        view.flags38 |= 1u;
      }
      if (entity.timelineCursorA8 == 2 && (entity.flags06 & 4u) != 0 &&
          environment.FUN_00267d38_playSound)
      {
        environment.FUN_00267d38_playSound(kFUN_0028b698_enemy8aHitCue, entity);
      }
      if ((entity.flags06 & 1u) == 0)
      {
        return;
      }
      if (haveRecord)
      {
        view.flags38 &= ~1u;
      }
      FUN_00225bf0_set_state_and_animation(entity, 1, 0);
    }

    // FUN_0028b568, type 0x8A state 5: **the death.** The clip ending latches
    // +0x06 bit 0x10 and arms a 0x3C0-tick corpse timer; when that runs out the
    // entity raises the fade-and-free bit and tidies its two attack links.
    //
    // The `+0x1AC` test is why the tidy-up has two shapes. FUN_0028b740 -- the
    // spit landing and growing a *second* Maneater -- stamps 2 there on the
    // child and points the child's +0x1A4 back at its parent. A plain 0x8A has
    // never been through that, so its +0x1AC is zero, its +0x1A4 is empty, and
    // the busy bit is deliberately *not* released: the corpse holds its record
    // until the entity is freed. FUN_0028b740 is not ported, so the port only
    // ever takes that first shape -- the byte is modelled anyway so the branch
    // is honest rather than assumed away.
    void FUN_0028b568_enemy8a_death(OriginalEntity &entity,
                                    const ActorEnvironment &environment,
                                    ActorEnvironment::BattleActorView &view,
                                    bool haveRecord)
    {
      if (haveRecord)
      {
        view.flags38 |= 1u;
      }
      if (entity.timelineCursorA8 == 2 && (entity.flags06 & 4u) != 0 &&
          environment.FUN_00267d38_playSound)
      {
        environment.FUN_00267d38_playSound(kFUN_0028b568_enemy8aDeathCue, entity);
      }

      const std::uint16_t flags = entity.flags06;
      if ((flags & 1u) != 0 && (flags & 0x10u) == 0)
      {
        entity.flags06 = static_cast<std::uint16_t>(flags | 0x10u);
        entity.fadeRamp62 = 0x3C0;
      }
      if ((entity.flags06 & 0x10u) == 0)
      {
        return;
      }

      entity.fadeRamp62 = static_cast<std::uint16_t>(FUN_0023a678_countdown(
          static_cast<std::int16_t>(entity.fadeRamp62), environment.frameTicks));
      if (entity.fadeRamp62 != 0)
      {
        return;
      }

      entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 0x0801u);
      EntityPool &pool = *environment.entityPool;

      // The two shapes of the tidy-up, and which one runs is +0x1AC. A Maneater
      // the scene placed ran FUN_0028ae10, which stamped 1 there, and owns the
      // seed at +0x1A8. A clone grown by FUN_0028b740 carries 2 instead, never
      // ran state 0, and owns nothing -- what it has at +0x1A4 is a link *back*
      // to its parent, and all it has to do is clear the parent's own +0x1A4 so
      // the Maneater can take a bite order again.
      if ((entity.enemySpawnFlag1ac & 1u) == 0)
      {
        if (entity.enemyAttackLink1a4 >= 0 &&
            static_cast<std::size_t>(entity.enemyAttackLink1a4) < pool.slotCount())
        {
          pool.slot(static_cast<std::size_t>(entity.enemyAttackLink1a4)).enemyAttackLink1a4 = -1;
        }
        return;
      }

      // With no clone and no seed left the record is finally released -- and
      // only here, which is why a Maneater that dies mid-spit holds its actor
      // busy until the seed is gone.
      if (entity.enemyAttackLink1a4 < 0 && entity.enemyAttackLink1a8 < 0 && haveRecord)
      {
        view.flags38 &= ~1u;
      }
      if (entity.enemyAttackLink1a8 >= 0 &&
          static_cast<std::size_t>(entity.enemyAttackLink1a8) < pool.slotCount())
      {
        OriginalEntity &seed = pool.slot(static_cast<std::size_t>(entity.enemyAttackLink1a8));
        if (seed.typeId00 == kManeaterSeedTypeId)
        {
          // The seed fades and frees itself the same way the corpse does.
          seed.halfword04 = static_cast<std::uint16_t>(seed.halfword04 | 0x0801u);
        }
      }
      entity.enemyAttackLink1a8 = -1;
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

    if (!enemy_action_check(entity, environment, view, haveRecord, true,
                            [&](std::int16_t action)
                            { FUN_0027f5c8_enemy80_action(entity, environment, view, action); }))
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
    const bool implemented = entity.state60 == 0 || entity.state60 == 1 ||
                             (haveRecord && (entity.state60 == 2 || entity.state60 == 3 ||
                                             entity.state60 == 5 || entity.state60 == 6 ||
                                             entity.state60 == 7 || entity.state60 == 8));
    trace.recordStateDispatch(entity.typeId00, entity.state60, handler, implemented);
    if (entity.state60 == 0)
    {
      enemy_state0(entity, slot, environment, false);
    }
    else if (haveRecord)
    {
      switch (entity.state60)
      {
      case 1:
        FUN_0027fa88_enemy80_turn(entity, environment, view);
        break;
      case 2:
        FUN_0027fb30_enemy80_leap(entity, slot, environment, view, trace);
        break;
      case 3:
        FUN_0027fdf8_enemy80_lunge(entity, slot, environment, view, trace);
        break;
      case 5:
        FUN_00280288_enemy80_recover_flight(entity, environment, view);
        break;
      case 6:
        FUN_00280428_enemy80_return(entity, environment, view);
        break;
      case 7:
        FUN_00280560_enemy80_death(entity, environment, view);
        break;
      case 8:
        FUN_00280628_enemy80_hit(entity, environment, view);
        break;
      default:
        break;
      }
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

    EntityPool &pool = *environment.entityPool;

    // uGpffffb052 bit 3, the "battle is over" broadcast: drop the seed still in
    // the air -- freeing it outright when it really is one, since a type 0x112
    // has no behaviour of its own to notice -- and go to state 5.
    if ((environment.sGpffffb052_battleFlags & 8u) != 0)
    {
      if (entity.enemyAttackLink1a8 >= 0 &&
          static_cast<std::size_t>(entity.enemyAttackLink1a8) < pool.slotCount())
      {
        if (pool.slot(static_cast<std::size_t>(entity.enemyAttackLink1a8)).typeId00 ==
            kManeaterSeedTypeId)
        {
          pool.releaseSlot(static_cast<std::size_t>(entity.enemyAttackLink1a8));
        }
        entity.enemyAttackLink1a8 = -1;
      }
      if (entity.state60 != 5)
      {
        entity.fadeRamp62 = 0;
        FUN_00225bf0_set_state_and_animation(entity, 5, 8);
      }
    }

    // The step. +0x06 bit 0 is "the animation came round", so this keys once a
    // loop rather than once a frame.
    if (entity.animationA0 == 1 && (entity.flags06 & 1u) != 0 && environment.FUN_00267d38_playSound)
    {
      environment.FUN_00267d38_playSound(kFUN_0028a958_stepCue, entity);
    }

    // The seed's whole clock. Type 0x112 has no actor handler, so the Maneater
    // that spat it walks its arc from here: 1 the frame it lands, which is when
    // the clone grows, -1 when it has finished sinking and freed itself.
    if (entity.enemyAttackLink1a8 >= 0 &&
        static_cast<std::size_t>(entity.enemyAttackLink1a8) < pool.slotCount())
    {
      const std::size_t seedSlot = static_cast<std::size_t>(entity.enemyAttackLink1a8);
      const std::int32_t seedState =
          FUN_002ec750_seed_flight(pool.slot(seedSlot), seedSlot, environment);
      if (seedState < 0)
      {
        entity.enemyAttackLink1a8 = -1;
      }
      else if (seedState == 1)
      {
        FUN_0028b740_grow_clone(entity, slot, environment);
      }
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

    if (!enemy_action_check(entity, environment, view, haveRecord, false,
                            [&](std::int16_t action)
                            { FUN_0028ac40_enemy8a_action(entity, environment, view, action); }))
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
    const bool implemented =
        entity.state60 == 0 || entity.state60 == 1 || entity.state60 == 3 ||
        entity.state60 == 5 || entity.state60 == 6 ||
        (haveRecord && (entity.state60 == 2 || entity.state60 == 4));
    trace.recordStateDispatch(entity.typeId00, entity.state60, handler, implemented);
    if (entity.state60 == 0)
    {
      enemy_state0(entity, slot, environment, true);
    }
    // Unlike the flyer's, both of the Maneater's damage states test +0x198 for
    // null themselves, so they run whether or not the entity has a record.
    else if (entity.state60 == 5)
    {
      FUN_0028b568_enemy8a_death(entity, environment, view, haveRecord);
    }
    else if (entity.state60 == 6)
    {
      FUN_0028b698_enemy8a_hit(entity, environment, view, haveRecord);
    }
    // So does state 3: a clone is never bound to an actor record at all, and
    // FUN_0028b0e8 never reads +0x198.
    else if (entity.state60 == 3)
    {
      FUN_0028b0e8_enemy8a_clone(entity, slot, environment);
    }
    else if (haveRecord)
    {
      switch (entity.state60)
      {
      case 1:
        FUN_0028af28_enemy8a_turn(entity, environment, view);
        break;
      case 2:
        FUN_0028afc0_enemy8a_bite(entity, slot, environment, view, trace);
        break;
      case 4:
        FUN_0028b420_enemy8a_spit(entity, slot, environment, view, trace);
        break;
      default:
        break;
      }
    }

    publish();
  }

} // namespace orphen::ported::entity
