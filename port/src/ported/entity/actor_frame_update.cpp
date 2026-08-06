#include "ported/entity/actor_frame_update.h"

#include "ported/script/object_registers.h"

#include <algorithm>
#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    // FUN_00239ce0 starts at &DAT_0058c260, which is the pool base plus
    // 2 * 0x1D8. Slot 0 is the lead player, updated by FUN_00251ed8 on its own
    // path; slot 1 is skipped with it.
    constexpr std::size_t kFirstTickedSlot = 2;

    // The guard bits, at the entity offsets FUN_00239ce0 tests them at.
    constexpr std::uint16_t kHidden02 = 0x0800;    // +0x02
    constexpr std::uint16_t kSuspended04 = 0x4000; // +0x04
    constexpr std::uint16_t kFading04 = 0x0800;    // +0x04
  } // namespace

  bool FUN_0023a068_freeze_gate(OriginalEntity &entity, std::uint32_t frameTicks)
  {
    const std::int8_t remaining = entity.freezeTimerBd;
    const bool frozen = remaining != 0;
    if (frozen)
    {
      entity.freezeTimerBd = static_cast<std::int8_t>(remaining - 1);
      entity.stateResetA4 = static_cast<std::uint16_t>(entity.stateResetA4 + frameTicks);
    }
    // The last frozen frame still runs the behavior.
    return frozen && remaining != 1;
  }

  void FUN_00225bc8_set_animation(OriginalEntity &entity, std::uint16_t animation)
  {
    entity.animationA0 = animation;
    entity.stateResetA4 = 999;
    entity.previousSubstateA2 = 0xFFFF;
    entity.flags06 = static_cast<std::uint16_t>(entity.flags06 & 0xFF38u);
    entity.substateFrameA8 = 0;
  }

  void FUN_00225bf0_set_state_and_animation(OriginalEntity &entity,
                                            std::uint16_t state,
                                            std::uint16_t animation)
  {
    entity.state60 = state;
    FUN_00225bc8_set_animation(entity, animation);
  }

  void FUN_0023a568_fade(EntityPool &pool, std::size_t slot, std::uint32_t frameTicks)
  {
    OriginalEntity &entity = pool.slot(slot);
    constexpr std::uint32_t kFullyFadedIn = 0x00FFFFFFu;

    if (entity.fadeColor138 != kFullyFadedIn)
    {
      // Fading in: the ramp climbs at four ticks per frame.
      const std::int32_t ramp =
          static_cast<std::int16_t>(entity.fadeRamp62 + static_cast<std::uint16_t>(frameTicks * 4u));
      entity.fadeRamp62 = static_cast<std::uint16_t>(ramp);
      if (ramp < 0x2000)
      {
        // `iVar2 = iVar3 + 0x1f; if (-1 < iVar3) iVar2 = iVar3; iVar2 >> 5` is
        // the compiler's signed divide-by-32, which truncates toward zero rather
        // than flooring. The bound above keeps the result under 0x100, so the
        // three shifted copies do not overlap.
        const std::uint32_t level = static_cast<std::uint32_t>(ramp / 32);
        entity.fadeColor138 = (level << 16) | (level << 8) | level;
      }
      else
      {
        entity.fadeColor138 = kFullyFadedIn;
        entity.fadeRamp62 = 0x0FE0;
      }
      return;
    }

    // Fading out: half the rate, and the slot is released at the bottom.
    const std::int32_t ramp =
        static_cast<std::int16_t>(entity.fadeRamp62 - static_cast<std::uint16_t>(frameTicks * 2u));
    entity.fadeRamp62 = static_cast<std::uint16_t>(ramp);
    if (ramp > 0x80)
    {
      entity.fadeLevel134 = static_cast<std::uint8_t>(ramp / 32);
      return;
    }

    // FUN_00265ec0. The original also runs the script's word-4 teardown entry
    // when the entity's +0x02 has bit 0x8000; the port does not drive that entry
    // yet, so this is a plain release.
    pool.releaseSlot(slot);
  }

  void FUN_002d1ea8_treasure_chest(OriginalEntity &entity, const ActorEnvironment &environment)
  {
    const auto flagSet = [&environment](std::uint32_t flagId) {
      return environment.eventFlag ? environment.eventFlag(flagId) : false;
    };

    // First tick: pick the closed or the already-opened pose from the flag.
    // FUN_00266240 leaves +0x94 at 0 for group-3 spawns, which is what gets us
    // here exactly once.
    if (entity.spawnParam94 == 0)
    {
      entity.animationA0 = flagSet(entity.eventFlagId198) ? 6 : 4;
      entity.spawnParam94 = 1;
      return;
    }

    if (entity.animationA0 == 4)
    {
      // Closed, watching the flag. Nothing here sets it -- the chest only
      // observes. The interaction path (script header word 3) is what opens it,
      // and the port does not drive that entry, so in practice a chest stays
      // closed. That is faithful, not a stub.
      if (flagSet(entity.eventFlagId198))
      {
        FUN_00225bc8_set_animation(entity, 5);
      }
      return;
    }

    if (entity.animationA0 != 5)
    {
      return; // 6 is terminal
    }

    // Opening. The original's contents effect is gated on cGpffffb6e1 == 0x23, a
    // global mode the port never enters, and its body builds a GS packet at
    // 0x70000000. The port has no primitive submission path, so that branch is
    // deliberately absent; with it absent +0x19E is never set and the timer tail
    // below never runs either. Both are kept so the shape survives.
    constexpr std::uint16_t kEffectDuration = 0x1680; // 0x120 frames at 0x20 ticks
    if (entity.effectActive19e != 0)
    {
      if (entity.effectTimer19c < kEffectDuration)
      {
        entity.effectTimer19c = static_cast<std::uint16_t>(entity.effectTimer19c + environment.frameTicks);
      }
      else
      {
        entity.effectActive19e = 0;
      }
    }
  }

  // FUN_0025ab68 (party members, types 0x03..0x07): freeze gate, then
  // PTR_LAB_0031e1d0[+0x60].
  //
  // Entities spawn in state 0, and state 0 -- like state 6 -- is 0x0025ABB8,
  // which is `jr ra; nop` in the executable. So an idle party member genuinely
  // does nothing every frame, and the room's characters keep the facing the
  // scene's init gave them through object register 13. That is the whole of
  // their visible behavior until something moves them out of state 0.
  void FUN_0025ab68_party_member(OriginalEntity &entity,
                                 const ActorEnvironment &environment,
                                 ActorTrace &trace)
  {
    if (FUN_0023a068_freeze_gate(entity, environment.frameTicks))
    {
      return;
    }

    const std::uint32_t handler = environment.dispatchTable->stateHandler(
        kPTR_LAB_0031e1d0_partyStates, kPartyStateCount, entity.state60);
    trace.recordStateDispatch(entity.typeId00, entity.state60, handler, handler == kLAB_0025abb8_noOp);
  }

  // FUN_002cd210: the type 0x62 enemy's state 0, which is its one-shot init.
  void FUN_002cd210_enemy62_init(OriginalEntity &entity, const ActorEnvironment &environment)
  {
    EntityPool &pool = *environment.entityPool;

    // Straight to the chase state.
    FUN_00225bf0_set_state_and_animation(entity, 3, 2);

    entity.enemyFlags1c8 = 1;
    entity.attackChance1c0 = 1000;
    entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 1u);
    entity.verticalAcceleration48 = kDAT_0035450c_enemyGravity;

    // +0x1A0 is the target, resolved from the pool index at +0x19C. The
    // original stores a pointer; the port stores the slot.
    entity.targetSlot1a0 = entity.targetIndex19c;

    // FUN_00267da0(+0x1B4, +0x20, 0xC): the home position, three floats copied
    // out of the live position. State 3 falls back to it when it has no target.
    entity.homeX1b4 = entity.positionX20;
    entity.homeZ1b8 = entity.positionZ24;
    entity.homeY1bc = entity.positionY28;

    // The companion clones. s01_e024's enemy asks for five, and an EE dump
    // confirms six type 0x62 entities in slots 23..28 -- the leader at radius
    // 0.180 and five clones at 0.126, which is exactly the leader's own scale
    // times DAT_00354510.
    const std::int32_t cloneCount = static_cast<std::int32_t>(entity.eventFlagId198);
    if (cloneCount <= 0 || environment.descriptors == nullptr)
    {
      return;
    }

    const std::int16_t typeId = entity.typeId00;
    const float cloneScale = entity.scale14c * kDAT_00354510_cloneScale;
    const float spawnX = entity.positionX20;
    const float spawnZ = entity.positionZ24;
    const float spawnY = entity.positionY28;
    const std::uint32_t rejectMask = entity.rejectTerrainMask74;
    const std::uint32_t requireMask = entity.requiredTerrainMask78;
    const std::size_t leaderSlot = environment.currentSlot;

    for (std::int32_t index = 0; index < cloneCount; ++index)
    {
      const std::size_t slot = pool.FUN_00265e28_allocate_and_initialize(typeId, *environment.descriptors);
      if (slot >= kEntitySlotCount)
      {
        break;
      }

      OriginalEntity &clone = pool.slot(slot);
      // FUN_002662e0: all five start stacked on the leader and spread out from
      // there once state 3 gives each its own random offset.
      clone.positionX20 = spawnX;
      clone.positionZ24 = spawnZ;
      clone.positionY28 = spawnY;
      clone.groundHeight4c = spawnY;

      // FUN_00229ef0(scale, clone): the descriptor's radius and height times the
      // scale. This is what makes the clones visibly smaller than the leader.
      clone.scale14c = cloneScale;
      clone.radius54 *= cloneScale;
      clone.height58 *= cloneScale;

      FUN_00225bf0_set_state_and_animation(clone, 3, 2);
      clone.verticalAcceleration48 = kDAT_0035450c_enemyGravity;
      clone.rejectTerrainMask74 = rejectMask;
      clone.requiredTerrainMask78 = requireMask;
      clone.attackChance1c0 = 500; // half the leader's, so clones attack less
      clone.targetSlot1a0 = static_cast<std::int32_t>(leaderSlot);
      clone.eventFlagId198 = 0; // a clone never spawns clones of its own
      clone.homeX1b4 = clone.positionX20;
      clone.homeZ1b8 = clone.positionZ24;
      clone.homeY1bc = clone.positionY28;
    }
  }

  // FUN_0023a320: step an angle toward a target, capped, with a half-degree dead
  // zone so it stops rather than jitters.
  float FUN_0023a320_approach_angle(float from, float to, float maxStep)
  {
    const float difference = orphen::ported::script::FUN_00216690_wrapAngle(to - from);
    if (difference > kAngleDeadZone)
    {
      return std::min(difference, maxStep);
    }
    if (difference < -kAngleDeadZone)
    {
      return std::max(difference, -maxStep);
    }
    return 0.0f;
  }

  // FUN_002cd3a0: type 0x62's state 3, the one it spends its life in. A hover
  // and chase, not a walk -- it picks a point near its target, turns toward it,
  // drives forward at a fixed speed and holds a height above the floor.
  void FUN_002cd3a0_enemy62_chase(OriginalEntity &entity, const ActorEnvironment &environment)
  {
    EntityPool &pool = *environment.entityPool;
    const std::uint32_t frameTicks = environment.frameTicks;

    // +0x1C4 == 1 hands off to state 2. Nothing in the port sets it.
    if (entity.alertState1c4 == 1)
    {
      entity.state60 = 2;
      return;
    }

    const auto randomValue = [&environment]() -> std::int32_t {
      return environment.random ? static_cast<std::int32_t>(environment.random() & 0x7FFFFFFF) : 0;
    };

    // +0x62 is the repath countdown. While it is running the target point is
    // left alone; when it expires a fresh one is chosen.
    if (static_cast<std::int16_t>(entity.fadeRamp62) > 0)
    {
      entity.fadeRamp62 =
          static_cast<std::uint16_t>(static_cast<std::int16_t>(entity.fadeRamp62) - static_cast<std::int16_t>(frameTicks));
    }
    else
    {
      // A random offset around the target: +/-1.0 horizontally and +/-0.25
      // vertically, all in hundredths.
      const float offsetX = static_cast<float>(randomValue() % 200 - 100) / 100.0f;
      const float offsetZ = static_cast<float>(randomValue() % 200 - 100) / 100.0f;
      const float offsetY = static_cast<float>(randomValue() % 0x32 - 0x19) / 100.0f;

      // 0x780 ticks between repaths, halved to 0x3C0 when +0x1C8 is clear.
      entity.fadeRamp62 = entity.enemyFlags1c8 != 0 ? 0x780 : 0x3C0;

      float goalX = entity.homeX1b4;
      float goalZ = entity.homeZ1b8;
      float goalY = entity.homeY1bc;

      // The target is a pool slot here rather than the original's pointer. Type
      // id 0 stands in for "no target", which is what *psVar7 == 0 tests.
      const std::size_t targetSlot = static_cast<std::size_t>(entity.targetSlot1a0);
      if (entity.targetSlot1a0 >= 0 && targetSlot < kEntitySlotCount)
      {
        const OriginalEntity &target = pool.slot(targetSlot);
        // FUN_002cd3a0 tests the *target's* terrain word (+0x6C), not its mask:
        // the enemy only commits to something standing on floor it is willing to
        // follow onto. With +0x6C unpublished this could never have worked.
        const bool terrainAgrees =
            entity.requiredTerrainMask78 == 0 ||
            (entity.requiredTerrainMask78 & target.flagWord6c) != 0;
        if (target.typeId00 != 0 && terrainAgrees)
        {
          goalX = target.positionX20;
          goalZ = target.positionZ24;
          // Three quarters of the way up the target, not its feet.
          goalY = target.positionY28 + target.height58 * 0.75f;
        }
      }

      goalX += offsetX;
      goalZ += offsetZ;
      goalY += offsetY;

      // Never pick a point below the floor under us: re-roll upward instead.
      if (goalY < entity.groundHeight4c)
      {
        goalY = entity.groundHeight4c + static_cast<float>(randomValue() % 0x32 + 0x19) / 100.0f;
      }

      entity.desiredFacing1a8 = std::atan2(goalZ - entity.positionZ24, goalX - entity.positionX20);
      entity.desiredHeight1ac = goalY;
    }

    // Bumping into something (+0x0C bits 0x202) scatters the facing by up to
    // about 63 degrees so a stuck actor works its way loose.
    if ((entity.collisionFlags0c & 0x202u) != 0)
    {
      const float scatter = static_cast<float>(randomValue() % 0x274) / 10.0f;
      const float wrapped = orphen::ported::script::FUN_00216690_wrapAngle(entity.desiredFacing1a8 + scatter);
      entity.facingRadians5c = wrapped;
      entity.desiredFacing1a8 = wrapped;
    }

    entity.facingRadians5c += FUN_0023a320_approach_angle(
        entity.facingRadians5c, entity.desiredFacing1a8, static_cast<float>(frameTicks) * kDAT_00354514_turnRate);

    // The attack roll. Aligned within 45 degrees, in range, and at a similar
    // height, then a 1-in-1000 style roll against +0x1C0 hands off to state 4.
    // State 4 is not ported, so this is left out rather than sending the entity
    // into a state that would do nothing; the condition is kept for the record.

    // Drive forward along the facing, always.
    const float step = static_cast<float>(frameTicks) * kDAT_00354524_moveSpeed;
    entity.desiredDeltaX30 += step * std::cos(entity.facingRadians5c);
    entity.desiredDeltaZ34 += step * std::sin(entity.facingRadians5c);

    // Hold the desired height. Above it by more than 0.005, sink; below by more
    // than 0.005, rise; inside the band, leave the vertical alone entirely.
    const float heightError = entity.positionY28 - entity.desiredHeight1ac;
    if (heightError > kDAT_00354528_hoverHigh)
    {
      entity.desiredDeltaY38 -= kDAT_0035452c_hoverDown;
    }
    else if (heightError < kDAT_00354530_hoverLow)
    {
      entity.desiredDeltaY38 += kDAT_00354534_hoverUp;
    }
  }

  // FUN_002cdb28: the type 0x62 wing flap, driven straight onto four bones
  // through the scripted-override table rather than through the animation.
  //
  // DAT_00326650 is {3, 4, 5, 6} and DAT_00326640 is {0.85, 0.15, -0.85, -0.15}
  // -- two mirror pairs, and grp_0091's bones 3/5 and 4/6 sit at x = -+0.0884
  // and -+0.0562, so the pairing is left and right of the same two joints.
  //
  // The override zeroes translation and sets scale to 1, so these bones pivot on
  // their parent's origin rather than their own. That is what the original does;
  // the offsets it discards are under a tenth of a unit.
  //
  // fGpffffa5e4 is 2*pi, so +0x1B0 is in degrees and the rotation is a plain
  // degrees-to-radians conversion. The duration passed to FUN_0020d8c0 is 0,
  // which leaves the countdown negative and makes every frame's override snap --
  // correct for something rewritten every frame.
  void FUN_002cdb28_wing_flap(OriginalEntity &entity, const ActorEnvironment &environment)
  {
    // (anim - 4) > 2 unsigned: animations 4, 5 and 6 drive the bones themselves.
    if (static_cast<std::uint16_t>(entity.animationA0 - 4) <= 2)
    {
      return;
    }
    if (environment.currentSlot >= environment.boneOverrides.size())
    {
      return;
    }
    orphen::ported::model::EntityBoneOverrides &overrides =
        environment.boneOverrides[environment.currentSlot];

    constexpr std::array<float, 4> kDAT_00326640_rollAngles{0.85f, 0.15f, -0.85f, -0.15f};
    constexpr std::array<std::size_t, 4> kDAT_00326650_bones{3, 4, 5, 6};
    constexpr float kfGpffffa5e4_twoPi = 6.283184051513672f;

    for (std::size_t index = 0; index < kDAT_00326650_bones.size(); ++index)
    {
      // Caller order: rotation xyz, translation xyz, scale.
      const std::array<float, orphen::ported::model::kPoseFieldCount> pose{
          (entity.wingPhase1b0 * kfGpffffa5e4_twoPi) / 360.0f,
          0.0f,
          kDAT_00326640_rollAngles[index],
          0.0f,
          0.0f,
          0.0f,
          1.0f};
      orphen::ported::model::FUN_0020d8c0_set_bone_override(
          overrides, kDAT_00326650_bones[index], pose, 0);
    }

    entity.wingPhase1b0 += static_cast<float>(environment.frameTicks) * 25.0f * 0.03125f;
    if (entity.wingPhase1b0 > 60.0f)
    {
      entity.wingPhase1b0 = -40.0f;
    }
  }

  // FUN_002cd0a0 (type 0x62): freeze gate, the +0xBE hit reaction, the +0x1C2
  // countdown, then PTR_FUN_00326660[+0x60].
  void FUN_002cd0a0_enemy62(OriginalEntity &entity,
                            const ActorEnvironment &environment,
                            ActorTrace &trace)
  {
    if (FUN_0023a068_freeze_gate(entity, environment.frameTicks))
    {
      return;
    }

    // +0xBE is damage taken since the last tick. Draining it to zero forces
    // state 6 and seeds the stagger. The port has no damage source, so this
    // never fires -- but it is the wrapper's first act, so it is kept.
    if (entity.pendingDamageBe != 0)
    {
      const std::int32_t remaining =
          static_cast<std::int32_t>(entity.staggerTimer12a) - static_cast<std::int32_t>(entity.pendingDamageBe);
      entity.staggerTimer12a = static_cast<std::uint16_t>(remaining);
      if (static_cast<std::int16_t>(entity.staggerTimer12a) < 1)
      {
        entity.staggerTimer12a = 0;
        FUN_00225bf0_set_state_and_animation(entity, 6, 4);
        entity.halfword04 = static_cast<std::uint16_t>((entity.halfword04 & 0xFFF7u) | 0x10u);
        entity.collisionFlags0c &= ~1u;
        entity.fadeLevel134 = 0x7C;
      }
      entity.fadeColor138 = 0xC0;
      entity.hitFlash1c2 = 0x1E0;
      entity.pendingDamageBe = 0;
    }

    // +0x1C2 counts the hit flash down, clearing the tint when it expires.
    if (entity.hitFlash1c2 != 0)
    {
      const std::int32_t remaining =
          static_cast<std::int32_t>(entity.hitFlash1c2) - static_cast<std::int32_t>(environment.frameTicks);
      entity.hitFlash1c2 = static_cast<std::uint16_t>(remaining);
      if (static_cast<std::int16_t>(entity.hitFlash1c2) < 1)
      {
        entity.hitFlash1c2 = 0;
        entity.fadeColor138 = 0;
      }
    }

    const std::uint32_t handler = environment.dispatchTable->stateHandler(
        kPTR_FUN_00326660_enemy62States, kEnemy62StateCount, entity.state60);

    const bool implemented = entity.state60 == 0 || entity.state60 == 3;
    trace.recordStateDispatch(entity.typeId00, entity.state60, handler, implemented);
    if (entity.state60 == 0)
    {
      FUN_002cd210_enemy62_init(entity, environment);
    }
    else if (entity.state60 == 3)
    {
      FUN_002cd3a0_enemy62_chase(entity, environment);
    }

    // FUN_002cd0a0 lines 39-40: the flap runs after the state handler, gated on
    // +0x08 bit 0 being clear and the state not being 6 (the death stagger).
    // FUN_002cde50, the periodic re-roll the same branch drives, is not ported.
    if ((entity.halfword08 & 1) == 0 && entity.state60 != 6)
    {
      FUN_002cdb28_wing_flap(entity, environment);
    }
  }

  // The shared non-player movement step.
  //
  // Behaviors do not move anything themselves; they accumulate a request into
  // +0x30/+0x34 (horizontal) and +0x38 (vertical) and the physics pass
  // integrates it. FUN_00239ce0's actors had no such pass in this port, so
  // everything a behavior asked for was silently discarded.
  //
  // **This is not FUN_002262c0.** It integrates the request and keeps a flying
  // actor above the floor, which is what type 0x62 needs; it does not do the
  // four-corner footprint sample, step-height acceptance, ceiling test or axis
  // fallback that the lead player's path does. Slot 0 still runs the real thing.
  // Porting FUN_002262c0 properly for slots 1..255 is the outstanding work, and
  // until then a non-player actor can pass through walls.
  void integrateNonPlayerMovement(OriginalEntity &entity, const ActorEnvironment &environment)
  {
    // +0x38 is *this frame's* vertical delta, not a velocity -- the player's path
    // zeroes it at the top of the update and again after applying it. Treating
    // it as a velocity is what sent these enemies into orbit: the hover nudge
    // accumulated every frame instead of being spent.
    //
    // **Gravity is deliberately not integrated here.** The EE dump shows all six
    // type 0x62 entities with +0x44 (vertical velocity) at exactly 0.000000
    // while +0x48 holds 0.00025, so FUN_002262c0 is not feeding gravity into
    // them at all -- their only vertical motion is the behavior's own
    // DAT_0035452c / DAT_00354534 nudge, which is why they sit just inside the
    // dead band below their target height. Adding gravity here pins them to the
    // floor, which the dump says is wrong.
    //
    // Which flag in FUN_002262c0 gates that is not identified yet. It matters
    // the moment a ground-walking non-player actor is ported; it does not matter
    // for a flyer, and inventing a gate would be worse than naming the gap.
    entity.positionX20 += entity.desiredDeltaX30;
    entity.positionZ24 += entity.desiredDeltaZ34;
    entity.positionY28 += entity.desiredDeltaY38;

    if (environment.terrainSurface)
    {
      const auto surface = environment.terrainSurface(entity.positionX20, entity.positionZ24);
      if (surface.has_value())
      {
        entity.previousGroundHeight50 = entity.groundHeight4c;
        entity.groundHeight4c = surface->height;
        // The same publish FUN_002262c0 does for the player. Non-player actors
        // need it too: a type 0x62 clone's target is its *leader*, and the chase
        // state gates on the target's +0x6C, so without this the clones sat
        // still. The EE dump has all six enemies reading 0x30010000 here.
        entity.flagWord6c = surface->terrainFlags;
        entity.flagWord70 = surface->terrainFlags;

        // The floor is still a floor even for a flyer.
        if (entity.positionY28 < surface->height)
        {
          entity.positionY28 = surface->height;
          entity.verticalVelocity44 = 0.0f;
        }
      }
    }

    entity.desiredDeltaX30 = 0.0f;
    entity.desiredDeltaZ34 = 0.0f;
    entity.desiredDeltaY38 = 0.0f;
  }

  bool actorHandlerIsImplemented(std::uint32_t handlerAddress)
  {
    switch (handlerAddress)
    {
    case kFUN_00239e78_noOp:
    case 0x002D1EA8u: // FUN_002d1ea8, type 0x3A
    case 0x0025AB68u: // FUN_0025ab68, party members
    case 0x002CD0A0u: // FUN_002cd0a0, the type 0x62 enemy
    case 0x00213720u: // FUN_00213720, type 0x19, the player's bandana
      return true;
    default:
      return false;
    }
  }

  const char *actorHandlerName(std::uint32_t handlerAddress)
  {
    switch (handlerAddress)
    {
    case kFUN_00239e78_noOp:
      return "FUN_00239e78 (no-op)";
    case 0x002D1EA8u:
      return "FUN_002d1ea8 (treasure chest)";
    case 0x0025AB68u:
      return "FUN_0025ab68 (party member)";
    case 0x002CD0A0u:
      return "FUN_002cd0a0 (enemy)";
    case 0x00213720u:
      return "FUN_00213720 (player bandana)";
    case kFUN_002cfe08_streamedProp:
      return "FUN_002cfe08 (map-streamed prop)";
    default:
      return nullptr;
    }
  }

  void FUN_00239ce0_update_actors(const ActorEnvironment &environment, ActorTrace &trace)
  {
    if (environment.entityPool == nullptr || environment.dispatchTable == nullptr)
    {
      return;
    }

    EntityPool &pool = *environment.entityPool;
    const ActorDispatchTable &table = *environment.dispatchTable;
    if (!table.available())
    {
      trace.noteTableUnavailable();
    }

    for (std::size_t slot = kFirstTickedSlot; slot < kEntitySlotCount; ++slot)
    {
      // The original's test is `'\0' < (char)status`, a signed compare, so
      // Allocated (0xFF, reserved but not yet initialised) is skipped along with
      // Free. Only a fully built, positive-typed entity ticks.
      if (pool.status(slot) != SlotStatus::ScriptSpawned)
      {
        continue;
      }

      OriginalEntity &entity = pool.slot(slot);
      if ((entity.descriptorFlags02 & kHidden02) != 0)
      {
        trace.recordHidden();
        continue;
      }
      if ((entity.halfword04 & kSuspended04) != 0)
      {
        trace.recordSuspended();
        continue;
      }
      if ((entity.halfword04 & kFading04) != 0)
      {
        trace.recordFading();
        FUN_0023a568_fade(pool, slot, environment.frameTicks);
        continue;
      }

      const ActorHandler handler = table.FUN_00239ce0_resolve(entity.typeId00);
      const bool implemented = handler.address != 0 && actorHandlerIsImplemented(handler.address);
      trace.recordDispatch(entity.typeId00, slot, handler, implemented);
      if (!implemented)
      {
        continue;
      }

      // iGpffffb650: handlers deeper in the tree read the slot being ticked.
      ActorEnvironment slotEnvironment = environment;
      slotEnvironment.currentSlot = slot;

      // FUN_00251ed8 clears these before running the player's state, and the
      // behaviors accumulate into them the same way. They are per-frame
      // requests, so they start at zero every frame.
      entity.desiredDeltaX30 = 0.0f;
      entity.desiredDeltaZ34 = 0.0f;
      entity.desiredDeltaY38 = 0.0f;

      switch (handler.address)
      {
      case 0x002D1EA8u:
        FUN_002d1ea8_treasure_chest(entity, slotEnvironment);
        break;
      case 0x0025AB68u:
        FUN_0025ab68_party_member(entity, slotEnvironment, trace);
        break;
      case 0x002CD0A0u:
        FUN_002cd0a0_enemy62(entity, slotEnvironment, trace);
        break;
      case 0x00213720u:
        if (environment.bandanaState != nullptr && environment.bandanaEnvironment &&
            slot < environment.boneOverrides.size())
        {
          FUN_00213720_bandana(entity, *environment.bandanaState,
                               environment.boneOverrides[slot],
                               environment.bandanaEnvironment(slot));
        }
        break;
      case kFUN_00239e78_noOp:
      default:
        break;
      }

      // An attached entity's +0x20..+0x28 is an offset in its parent bone's
      // space, not a world position -- FUN_0020cdc0's middle branch is what
      // says so. Integrating a movement request into it would drag the
      // attachment point off the bone.
      if (entity.parentSlot192 < 0)
      {
        integrateNonPlayerMovement(entity, environment);
      }
    }
  }

} // namespace orphen::ported::entity
