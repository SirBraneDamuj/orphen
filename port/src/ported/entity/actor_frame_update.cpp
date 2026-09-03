#include "ported/entity/actor_frame_update.h"

#include "ported/entity/entity_collision.h"
#include "ported/entity/party_follower.h"
#include "ported/original_frame_timing.h"
#include "ported/player/original_player_controller.h"
#include "ported/script/object_registers.h"

#include <algorithm>
#include <bit>
#include <iomanip>
#include <iostream>
#include <array>
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

    // FUN_00225c90's "the last timeline entry finished" latch on +0x06. Type
    // 0x42's whole state machine hangs off it.
    constexpr std::uint16_t kAnimationComplete06 = 0x0001;

    // DAT_00352998, read out of the EE dump: pi. FUN_00256130 puts it in the
    // blade's +0x158, the roll of its root matrix.
    constexpr float kDAT_00352998_bladeRoll = 3.14159250259399414f;

    // The blade's glow ramp, entity +0x62, divided by 32 to get the light's
    // colour byte: 0x1000 is 128 grey and 0x1FE0 is 255 white.
    constexpr std::uint16_t kBladeGlowStart = 0x1000;
    constexpr std::uint16_t kBladeGlowEnd = 0x1fe0;

    // The radius FUN_00256130 gives the blade's light slot, 0x40000000.
    constexpr float kBladeLightRadius = 2.0f;

    // DAT_0035468c, read out of eeMemory.bin: the magic projectile's hit box
    // half-extent in the horizontal plane.
    constexpr float kDAT_0035468c_hitExtent = 0.150000005960464478f;

    // The type 0x62 death tumble, read out of eeMemory.bin: uGpffffa5dc is the
    // pitch it falls toward (pi/2, flat on its back), fGpffffa5d8 the pitch step
    // per tick and fGpffffa5e0 the spin -- exactly twice the pitch rate, so it
    // rolls through half a turn while it tips over.
    constexpr float kuGpffffa5dc_deathPitchTarget = 1.57079601287841797f;
    constexpr float kfGpffffa5d8_deathPitchRate = 0.00545415282249450684f;
    constexpr float kfGpffffa5e0_deathSpinRate = 0.0109083056449890137f;

    // ---- type 0x44, the homing magic projectile ---------------------------
    // The constant block at 0x0035467C, plus the two gp-relative values
    // FUN_002d2e00 seeds the entity from. Every one read out of s01_e24.bin.
    constexpr float kDAT_0035467c_chargeOrbitBias = 3.14159250259399414f; // pi
    constexpr float kDAT_00354680_chargeOrbitStep = 0.000099999997f;
    constexpr float kDAT_00354684_chargeGrowth = 0.100000001f;
    constexpr float kDAT_00354688_trailShrink = 0.0500000007f;
    constexpr float kDAT_003546a0_turnRamp = 0.00499999989f;
    constexpr float kDAT_003546a4_turnMax = 0.349065989f; // 20 degrees
    constexpr float kuGpffffa73c_projectileExtent = 0.00999999978f;
    constexpr float kuGpffffa740_projectileSpeed = 0.00179999997f;
    constexpr float kProjectileLightRadius = 2.0f;
    // FUN_002d2ca8's search: ten units, and a 60-degree elevation cone
    // (fGpffffa738) for anything within two units of the projectile's height.
    constexpr float kDAT_002d2ca8_searchRadius = 10.0f;
    constexpr float kfGpffffa738_elevationCone = 1.04719734191894531f;
    constexpr std::uint16_t kProjectileHitCooldown = 0x00a0;
    constexpr std::uint16_t kProjectileHomingTicks = 0x2580;
    constexpr std::int16_t kProjectileMaxLifetime = 0x2580;

    // FUN_002d1ea8's camera flourish, from the constant block at 0x00354668.
    constexpr float fGpffffa6f8_firstSwing = 1.570796012878418f;   // pi/2
    constexpr float fGpffffa6fc_secondSwing = 0.785398006439209f;  // pi/4
    constexpr float fGpffffa700_riseAtEnd = 0.2f;
    constexpr float fGpffffa704_lookHeight = 0.3f;
    // Inline in FUN_002d1ea8: the eye closes a quarter unit by the middle
    // control point and another quarter by the last.
    constexpr float kSwingCloseIn = 0.25f;
    // The zoom curve, in pre-FUN_00218230 units. 1.0 is the shipped projection
    // scale, so this ends at three times it.
    constexpr float kZoomStart = 1.5f;
    constexpr float kZoomMiddle = 2.0f;
    constexpr float kZoomEnd = 3.0f;
    // FUN_002d1ea8:97. 0x1680 ticks is 180 nominal frames -- three seconds.
    constexpr std::uint16_t kSwingDuration = 0x1680;
    // cGpffffb6e1 == 0x23, the script camera. Without one already installed the
    // chest leaves the camera alone; that is the "no item" look.
    constexpr std::uint8_t kScriptCameraSubMode = 0x23;
    // FUN_002d59e0 == FUN_00267d38(0x9F, chest): the contents cue, bank 0
    // program 11 note 60. It plays whether or not the chest has an item.
    constexpr std::uint16_t kChestContentsCue = 0x9F;
    // FUN_002cde50 / FUN_002cde40: type 0x62's wing beat and its death cry.
    constexpr std::uint16_t kEnemyWingCue = 0x196;
    constexpr std::uint16_t kEnemyDeathCue = 0x197;

    // DAT_00352434: the step an actor will climb in one move, 0.26. A global,
    // not a per-entity field -- +0x80 is the slope limit, not this.
    constexpr float kDAT_00352434_stepHeight = 0.26f;

    // DAT_00318ad0. Sixteen (x, z) pairs indexed by the embedded-corner mask,
    // read straight out of the executable. Corner order is FUN_00227070's:
    // 0 = (x-r, z-r), 1 = (x+r, z-r), 2 = (x+r, z+r), 3 = (x-r, z+r), so bit
    // `n` means "corner n has ground above the feet". Every non-zero entry is
    // +/-0.18 -- the push is a fixed step away from the buried side, not a
    // penetration-depth resolve.
    struct PushOut
    {
      float x;
      float z;
    };
    constexpr PushOut kDAT_00318ad0_pushOut[16] = {
        {0.00f, 0.00f},  {0.18f, 0.18f},   {-0.18f, 0.18f},  {0.00f, 0.18f},
        {-0.18f, -0.18f}, {-0.18f, 0.18f}, {-0.18f, 0.00f},  {-0.18f, 0.18f},
        {0.18f, -0.18f}, {0.18f, 0.00f},   {0.18f, 0.18f},   {0.18f, 0.18f},
        {0.00f, -0.18f}, {0.18f, -0.18f},  {-0.18f, -0.18f}, {0.00f, 0.00f},
    };

    // DAT_0035242c, the quarter turn the mask-0xF fallback rotates by.
    constexpr float kDAT_0035242c_quarterTurn = 1.570796012878418f;

    // DAT_00352428. FUN_002262c0:107 substitutes it whenever the vertical
    // velocity lands on exactly zero, so the sign survives the next subtract.
    constexpr float kDAT_00352428_velocityFloor = -1.0e-05f;
  } // namespace

  // Defined further down; the enemy's clone loop calls it before its definition.
  void FUN_00229ef0_set_scale(OriginalEntity &entity, float scale,
                              const EntityDescriptorTable *descriptors);


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
    entity.timelineCursorA8 = 0;
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

  namespace
  {
    // FUN_002d1ea8:46-91. Three control points for the eye and one for the
    // look-at, handed to FUN_00217fe8 as a three-second path.
    //
    // The geometry is all relative to where the player's cutscene left the
    // camera: the swing keeps the current elevation, orbits 90 degrees and then
    // another 45 about the chest, closes a quarter unit at each step, and rises
    // 0.2 at the end. The look-at drops from the player's chest height to the
    // treasure's, 0.3 above the chest's own origin.
    void startContentsCameraSwing(OriginalEntity &entity,
                                  orphen::ported::camera::OriginalFieldCamera &camera)
    {
      const auto &pose = camera.pose();
      const float toEyeX = pose.eye.x - pose.target.x;
      const float toEyeY = pose.eye.y - pose.target.y;
      const float distance = std::sqrt(toEyeX * toEyeX + toEyeY * toEyeY) - kSwingCloseIn;
      const float azimuth = std::atan2(toEyeY, toEyeX);

      const float firstAngle =
          orphen::ported::script::FUN_00216690_wrapAngle(azimuth - fGpffffa6f8_firstSwing);
      const float secondAngle =
          orphen::ported::script::FUN_00216690_wrapAngle(firstAngle - fGpffffa6fc_secondSwing);

      const std::array<orphen::ported::psm2::Vec3, 3> eyePoints{{
          pose.eye,
          {entity.positionX20 + distance * std::cos(firstAngle),
           entity.positionZ24 + distance * std::sin(firstAngle),
           pose.eye.z},
          {entity.positionX20 + (distance - kSwingCloseIn) * std::cos(secondAngle),
           entity.positionZ24 + (distance - kSwingCloseIn) * std::sin(secondAngle),
           pose.eye.z + fGpffffa700_riseAtEnd},
      }};
      const std::array<orphen::ported::psm2::Vec3, 1> lookAtPoints{{
          {entity.positionX20, entity.positionZ24, entity.positionY28 + fGpffffa704_lookHeight},
      }};
      const std::array<float, 3> rollValues{{0.0f, 0.0f, 0.0f}};
      const std::array<float, 3> zoomScales{{kZoomStart, kZoomMiddle, kZoomEnd}};

      camera.FUN_00217e18_release_manual_camera(false);
      camera.FUN_00217fe8_set_camera_path(eyePoints, rollValues, zoomScales, lookAtPoints);

      entity.effectTimer19c = 0;
      entity.effectActive19e = 1;
    }
  } // namespace

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

    // Opening. On the keyframe that carries the 0x100 event marker the chest
    // takes the camera off the player's cutscene and swings it round itself --
    // but only when it has something in it. That is the whole difference
    // between the two chest cutscenes: an empty chest never reaches this, so
    // its camera stands where FUN_00254db0 put it.
    if ((entity.flagsAa & 0x0100) != 0 && (entity.flags06 & 0x0008) != 0)
    {
      if (environment.FUN_00267d38_playSound)
      {
        environment.FUN_00267d38_playSound(kChestContentsCue, entity); // FUN_002d59e0
      }
      if (entity.recordId130 >= 0 && environment.camera != nullptr &&
          environment.camera->cGpffffb6e1_subMode() == kScriptCameraSubMode)
      {
        startContentsCameraSwing(entity, *environment.camera);
      }
    }

    if (entity.effectActive19e != 0)
    {
      // FUN_00218158 is called with the timer *before* it advances, so the
      // first frame samples the path at exactly zero.
      if (environment.camera != nullptr)
      {
        environment.camera->FUN_00218158_step_camera_path(entity.effectTimer19c, kSwingDuration);
      }
      if (entity.effectTimer19c < kSwingDuration)
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

      // FUN_00229ef0(leaderScale * DAT_00354510, clone): the *descriptor's*
      // radius and height times the scale, not the clone's own scaled again.
      // This is what makes the clones visibly smaller than the leader.
      FUN_00229ef0_set_scale(clone, cloneScale, environment.descriptors);

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

  // FUN_002cda60: type 0x62's state 6, where a killed flyer goes.
  //
  // Two halves, and which one runs is decided by +0x0C bit 0 -- the grounded
  // flag the physics pass sets. FUN_002cd0a0 *clears* that bit on the frame it
  // kills the flyer, so the death always starts in the air:
  //
  //   airborne  pitch the body over toward pi/2 and spin it about its own
  //             facing, both capped per tick. Nothing here drives it downward;
  //             the gravity in +0x48 is what brings it down, which is why a
  //             flyer killed over a pit keeps tumbling.
  //
  //   grounded  swap to animation 5, the landing, and stop drawing the pitch.
  //             When *that* animation completes, +0x06 is assigned 0x10 -- an
  //             assignment, not an or, so every other animation latch is
  //             dropped with it -- and +0x04 gets bit 0x800, which is what
  //             hands the slot to FUN_0023a568 to fade out and free itself.
  //
  // The animation the flyer is *in* while airborne is 4, set by FUN_002cd0a0's
  // kill branch, not by anything here. eeMemory.bin catches two flyers in
  // exactly that state: slots 24 and 25, state 6, animation 4, +0x0C bit 0
  // clear, +0x134 at 0x7C and +0x138 at 0xC0.
  void FUN_002cda60_enemy62_death(OriginalEntity &entity, const ActorEnvironment &environment)
  {
    if ((entity.collisionFlags0c & 1u) != 0)
    {
      if (entity.animationA0 == 5)
      {
        if ((entity.flags06 & kAnimationComplete06) != 0)
        {
          entity.flags06 = 0x0010;
          entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | kFading04);
        }
      }
      else
      {
        FUN_00225bc8_set_animation(entity, 5);
        entity.rotationX154 = 0.0f;
        // +0x08 bit 0x10: "not drawn last frame", which makes the pose filter
        // snap to the landing rather than easing into it from the tumble.
        entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 0x0010u);
      }
      return;
    }

    const float ticks = static_cast<float>(environment.frameTicks);
    entity.rotationX154 += FUN_0023a320_approach_angle(
        entity.rotationX154, kuGpffffa5dc_deathPitchTarget, ticks * kfGpffffa5d8_deathPitchRate);
    entity.facingRadians5c = orphen::ported::model::FUN_00216690_wrap_angle(
        entity.facingRadians5c + ticks * kfGpffffa5e0_deathSpinRate);
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
        if (environment.FUN_00267d38_playSound)
        {
          environment.FUN_00267d38_playSound(kEnemyDeathCue, entity); // FUN_002cde40
        }
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

    const bool implemented =
        entity.state60 == 0 || entity.state60 == 3 || entity.state60 == 6;
    trace.recordStateDispatch(entity.typeId00, entity.state60, handler, implemented);
    if (entity.state60 == 0)
    {
      FUN_002cd210_enemy62_init(entity, environment);
    }
    else if (entity.state60 == 3)
    {
      FUN_002cd3a0_enemy62_chase(entity, environment);
    }
    else if (entity.state60 == 6)
    {
      FUN_002cda60_enemy62_death(entity, environment);
    }

    // FUN_002cd0a0 lines 39-52: the flap runs after the state handler, gated on
    // +0x08 bit 0 being clear and the state not being 6 (the death stagger),
    // and the same branch retriggers the wing cue.
    if ((entity.halfword08 & 1) == 0 && entity.state60 != 6)
    {
      FUN_002cdb28_wing_flap(entity, environment);

      // The buzz. +0x1C6 is a period, rolled once to 0x18..0x1F frames, and
      // the cue fires whenever the *global* frame counter divides by it -- so
      // each enemy drones at its own rate and they beat against each other.
      // The waveform is 1.3 s long against a period of 24..31 frames, so the
      // repeats overlap into a continuous sound rather than a pulse.
      if (entity.repathTimer1c6 == 0 && environment.random)
      {
        entity.repathTimer1c6 = static_cast<std::uint16_t>((environment.random() & 7) + 0x18);
      }
      if (entity.repathTimer1c6 != 0 && environment.FUN_00267d38_playSound &&
          environment.DAT_003555b4_frameCounter % entity.repathTimer1c6 == 0)
      {
        environment.FUN_00267d38_playSound(kEnemyWingCue, entity); // FUN_002cde50
      }
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
  bool gPushProbe = false;

  void integrateNonPlayerMovement(OriginalEntity &entity, const ActorEnvironment &environment,
                                  std::size_t slot)
  {
    // FUN_002262c0:0x00226304 -- the same +0x04 bit 0x100 gate the lead's copy
    // carries, and it comes before everything, including the clamps. An entity
    // wearing it keeps its scripted position and its scripted +0x4C; nothing
    // resamples the floor under it and its movement request is neither spent nor
    // cleared.
    if ((entity.halfword04 & 0x0100u) != 0)
    {
      return;
    }

    // FUN_002262c0:93. A frozen actor returns before anything else happens, so
    // +0x0C, +0x30 and +0x34 all keep the values last frame left them. The
    // original's workspace clear at :37 is ahead of this return, but the only
    // write-back to +0x0C is at :628, past it, so the entity's own copy is
    // untouched either way.
    if (entity.freezeTimerBd != 0)
    {
      return;
    }

    // **+0x0C is rebuilt every frame, not accumulated into.** FUN_002262c0
    // seeds a workspace word to zero at :37 and stores it over +0x0C at :628;
    // nothing ever ORs into the entity's copy in place. Leaving the bits to
    // pile up made them permanent, and the party follower is the reader that
    // notices: FUN_0025a500 counts consecutive stuck frames off +0x0C bits
    // 0x262 and gives up to the recovery state at five. Once a follower had
    // been blocked once -- by the walls either side of a cutscene pose, say --
    // the flags never came down again, so it counted to five standing on open
    // floor and stayed in state 6 for the rest of the scene.
    entity.collisionFlags0c = 0;

    // FUN_002262c0's entity-vs-entity clamps, before the request is spent.
    // They only narrow +0x30/+0x34, so an entity that asked for nothing is
    // untouched and the pool sweep is skipped entirely.
    if (environment.entityPool != nullptr)
    {
      FUN_002262c0_clamp_movement_against_entities(*environment.entityPool, slot);
    }

    // +0x38 is *this frame's* vertical delta, not a velocity -- the player's path
    // zeroes it at the top of the update and again after applying it. Treating
    // it as a velocity is what sent these enemies into orbit: the hover nudge
    // accumulated every frame instead of being spent.
    //
    // Gravity is integrated above, gated on +0x04 bit 3 -- see the note there.
    // The type 0x62 flyers carry that bit, which is why they still sit in the
    // dead band below their target height rather than on the floor.
    //
    // FUN_002262c0:482. +0x50 takes the previous +0x4C every frame, whether or
    // not the ground was resampled.
    entity.previousGroundHeight50 = entity.groundHeight4c;

    // FUN_002262c0:99-113, the gravity integrator. It is gated on **+0x04 bit
    // 3** and nothing else -- that is the flag the port had a note about not
    // having identified. eeMemory.bin settles it: the six type 0x62 enemies
    // read +0x04 = 0x000b, bit 3 set, so they never fall; every character in
    // the scene reads +0x48 = 0.00075 and +0x44 = 0, which is what the landing
    // clamp below leaves behind once they are resting on the floor.
    //
    //   dt  = DAT_003555bc * 0.125
    //   +0x38 += v * dt - (g * dt) * dt * 0.5
    //   v     -= g * dt,  and is nudged off exact zero so it stays signed
    //
    // Without this a non-player actor that stepped onto anything never came
    // down again: the landing clamp only ever raised it.
    if ((entity.halfword04 & 0x0008u) == 0)
    {
      const float physicsStep = orphen::ported::physicsStepForFrameTicks(environment.frameTicks);
      const float delta = entity.verticalAcceleration48 * physicsStep;
      entity.desiredDeltaY38 += entity.verticalVelocity44 * physicsStep - delta * physicsStep * 0.5f;
      float velocity = entity.verticalVelocity44 - delta;
      if (velocity == 0.0f)
      {
        velocity = kDAT_00352428_velocityFloor;
      }
      entity.verticalVelocity44 = velocity;
    }

    // ---- FUN_002262c0:111-205, the embedded-corner push-out ----------------
    //
    // This is how an actor that a cutscene drops *inside* scenery gets back
    // out, and it was the last missing piece of "Magnus stands on the crates".
    //
    // It is reached from :112 on either of two gates:
    //
    //   DAT_003555d0 != 0 && (entity +0x08 & 0x20)   any actor, any frame a
    //                                                collision group moved
    //   the entity is the lead && (DAT_003555b4 & 0x3F) == 0   every 64 frames
    //
    // and the whole thing is skipped when DAT_003555d1 is set. The first gate
    // is the interesting one: `DAT_003555d0` is raised by FUN_00208450 for any
    // group with a live dirty byte, so it means **movable collision moved this
    // frame** -- a door swinging, s01_e012's sea rolling. That is exactly when
    // something may have been swallowed and needs ejecting, and it is why an
    // EE dump almost always reads 0 here: the flag is transient, up for the
    // frames a group is animating and down again after. A previous pass read
    // that zero out of three dumps and concluded the branch was dead. It is
    // not; it is just rarely sampled.
    //
    // What it does: sample the four footprint corners where the actor already
    // stands, and note which of them have ground *above* the feet -- those are
    // embedded in something. The 4-bit mask indexes DAT_00318ad0, sixteen
    // (x, z) pairs of 0 and +/-0.18 that point away from the embedded side:
    //
    //     entity +0x30 = +0x30 * 0.5 + table[mask].x
    //     entity +0x34 = +0x34 * 0.5 + table[mask].z
    //
    // It is a movement *request*, not a teleport, so the velocity section below
    // spends it the same frame with the full wall and step logic -- the actor
    // is pushed out legally or not at all. Worked example, confirmed against
    // hardware: opcode 0x55 puts Magnus at (5.546, 0.128) with a 0.15 radius,
    // which buries corners 1 and 2 in crate primitive 1114 at -0.5. Mask 6,
    // table entry (-0.18, 0.00), and he lands at 5.366 -- where his corner
    // clears the crate's edge by 1e-4 and the mask reads 0. That 1e-4 is also
    // why FUN_00227d28 must have no tolerance: with slack he never stops.
    //
    // Mask 0xF -- every corner buried, so there is no "away" -- takes the
    // fallback at :160-195 instead: probe half a unit back along the facing,
    // rotating by pi/2 up to four times, and take the first heading whose
    // corners are all at or below the feet.
    // (This function is the non-player path, so the lead's every-64-frames leg
    // of the same branch is not reachable here -- slot 0 runs its own copy.)
    if (environment.FUN_00227390_corner_sample && environment.DAT_003555d0_collisionGroupMoved &&
        (entity.halfword08 & 0x0020u) != 0)
    {
      const auto cornersAt = [&entity, &environment](float x, float z) {
        return environment.FUN_00227390_corner_sample(x, z, entity.positionY28, entity.height58,
                                                      entity.radius54, entity.halfword04,
                                                      entity.rejectTerrainMask74);
      };
      const auto maskAt = [&entity](const std::optional<ActorEnvironment::TerrainSurface> &at) {
        unsigned mask = 0;
        // A single-point entity (+0x04 bit 1) never fills the four corner
        // slots. The original reads them anyway -- it just gets whatever the
        // last four-corner caller left in the shared workspace -- but the port
        // has no such residue, so reading zeroes here would call every corner
        // embedded and take the mask-0xF fallback on an actor standing on open
        // floor. Treat "not sampled" as "nothing embedded", which is what the
        // stale workspace amounts to in practice.
        if (!at.has_value() || !at->sampledFourCorners)
        {
          return mask;
        }
        for (std::size_t corner = 0; corner < 4; ++corner)
        {
          if (entity.positionY28 < at->cornerHeights[corner])
          {
            mask |= 1u << corner;
          }
        }
        return mask;
      };

      const auto sampled = cornersAt(entity.positionX20, entity.positionZ24);
      const unsigned mask = maskAt(sampled);
      if (gPushProbe)
      {
        // Mirrors a PCSX2 execute breakpoint at 0x002265E4, where $v1 is this
        // mask, $s1 the entity and $s0 the workspace whose +0x34..+0x40 are
        // these four corner heights. Same fields, same order, so the two logs
        // diff directly.
        const auto savedPrecision = std::cout.precision(9);
        std::cout << "[push] frame " << environment.frameNumber << " slot " << slot
                  << " pos (" << entity.positionX20 << ", " << entity.positionZ24
                  << ") feet " << entity.positionY28 << " (0x" << std::hex
                  << std::bit_cast<std::uint32_t>(entity.positionY28) << std::dec
                  << ") g4c " << entity.groundHeight4c << " (0x" << std::hex
                  << std::bit_cast<std::uint32_t>(entity.groundHeight4c) << std::dec
                  << ") r " << entity.radius54
                  << " h " << entity.height58 << " corners";
        for (std::size_t corner = 0; corner < 4; ++corner)
        {
          std::cout << ' '
                    << (sampled.has_value() && sampled->sampledFourCorners
                            ? sampled->cornerHeights[corner]
                            : 0.0f);
        }
        std::cout << " prims";
        for (std::size_t corner = 0; corner < 4; ++corner)
        {
          std::cout << ' '
                    << (sampled.has_value() ? sampled->cornerPrimitives[corner] : -1);
        }
        std::cout << " mask " << mask << " table (" << kDAT_00318ad0_pushOut[mask].x
                  << ", " << kDAT_00318ad0_pushOut[mask].z << ") delta ("
                  << entity.desiredDeltaX30 << ", " << entity.desiredDeltaZ34 << ")"
                  << "\n";
        std::cout.precision(savedPrecision);
      }
      if (mask == 0xFu)
      {
        // FUN_002262c0:160-195. DAT_0035242c is pi/2, and the step back is half
        // a unit against the facing.
        float heading = entity.facingRadians5c;
        for (int attempt = 0; attempt < 4; ++attempt)
        {
          const float offsetX = -(std::cos(heading) * 0.5f);
          const float offsetZ = -(std::sin(heading) * 0.5f);
          if (maskAt(cornersAt(entity.positionX20 + offsetX, entity.positionZ24 + offsetZ)) == 0)
          {
            entity.desiredDeltaX30 = offsetX;
            entity.desiredDeltaZ34 = offsetZ;
            break;
          }
          heading += kDAT_0035242c_quarterTurn;
        }
      }
      else if (mask != 0)
      {
        entity.desiredDeltaX30 = entity.desiredDeltaX30 * 0.5f + kDAT_00318ad0_pushOut[mask].x;
        entity.desiredDeltaZ34 = entity.desiredDeltaZ34 * 0.5f + kDAT_00318ad0_pushOut[mask].z;
        if (environment.pushOutCounter != nullptr)
        {
          ++*environment.pushOutCounter;
        }
      }
    }

    const float startX = entity.positionX20;
    const float startZ = entity.positionZ24;

    // A non-player actor resamples the ground **only when it is moving**.
    //
    // FUN_002262c0 reaches FUN_00227390 from exactly two places: the velocity
    // loop at :230-450, which runs only when +0x30 / +0x34 are non-zero, and the
    // stationary branch at :112-122, which is gated on `DAT_003555d0 != 0 &&
    // (entity +0x08 & 0x20)`. DAT_003555d0 is **0** in eeMemory.bin, so the
    // stationary branch never runs for anybody and only the player gets the
    // periodic refresh at :114.
    //
    // So a script-placed actor that never moves keeps the +0x4C its placement
    // opcode gave it, for the whole scene. That is directly visible in the dump:
    // slots 82 and 84 have +0x0A = -1 and +0x6C = 0 -- never ground-queried at
    // all -- and sit at the heights their 0x54 authored, one of them 0.30 above
    // a floor a query would have found. Sampling them every frame is what put
    // Volcan at -1.00 instead of -1.20.
    //
    // (The one other resample, FUN_002262c0:41-85's lift block, additionally
    // requires the cached primitive's +0x13 to be non-zero. It is 0 for every
    // primitive in s01_e012, so that block never fires here either.)
    const bool movedHorizontally = entity.desiredDeltaX30 != 0.0f || entity.desiredDeltaZ34 != 0.0f;
    std::optional<ActorEnvironment::TerrainSurface> surface;
    if (movedHorizontally && environment.terrainSurface)
    {
      const auto sampleAt = [&entity, &environment](float x, float z) {
        return environment.terrainSurface(x, z, entity.positionY28, entity.height58,
                                          entity.radius54, entity.halfword04,
                                          entity.rejectTerrainMask74);
      };

      // FUN_002262c0:0x00226cb4, the gate in front of the whole upward-step
      // branch: `if ((float)puVar11[2] <= *(float *)(iVar12 + 0x80))`, where
      // workspace +0x08 is the destination surface's stored slope. The lead's
      // copy of this loop has carried it for a while; this one had not, because
      // until the party follower there was no ground-*walking* non-player actor
      // and a flyer never notices.
      //
      // Without it the step below raises the actor onto whatever the scan
      // answered with, so a follower walking into the shop's counter ratcheted
      // up it a tenth of a unit per frame and ended the scene standing in the
      // air.
      const auto walkable = [&entity](const std::optional<ActorEnvironment::TerrainSurface> &at) {
        return at.has_value() && at->slopeAngle <= entity.slopeLimit80 &&
               at->height - entity.positionY28 < kDAT_00352434_stepHeight;
      };

      // **A refused move is retried on a rotated heading, not split per axis.**
      //
      // FUN_002262c0's whole velocity section is one `do { } while (true)` and
      // the wall case falls through to the ladder at 0x00226b58: five headings
      // derived from the request's own, each re-running the entity clamps and
      // FUN_00227390 in full. `$s4` is the attempt index and the constants come
      // straight out of the block at 0x0035243c:
      //
      //   0   heading            speed x 0.3
      //   1   heading + 20 deg   speed x 0.7
      //   2   heading - 20 deg   speed unchanged -- case 2 writes the heading
      //                          register and leaves the speed one alone
      //   3   heading + 60 deg   speed x 0.5
      //   4   heading - 60 deg   speed unchanged
      //   5   give up
      //
      // That is how an actor gets around a wall: not by keeping one axis, but by
      // fanning out either side of where it wanted to go. The port used to try X
      // alone and then Z alone, which is a different shape *and* recorded itself
      // in the wrong bits -- 0x20 and 0x40 belong to the entity blockers
      // (FUN_00228380 / FUN_00228838), not to terrain. A follower reads
      // `+0x0C & 0x60` as "an actor is in my way, queue behind it" and `& 0x262`
      // as its stuck counter, so inventing those two bits both sent it down the
      // wrong branch and made every wall graze count toward giving up.
      constexpr float kDAT_0035243c_firstRetryScale = 0.3f;
      constexpr float kDAT_00352440_narrowScale = 0.7f;
      constexpr float kDAT_00352444_narrowTurn = 0.3490658f; // +20 degrees
      constexpr float kDAT_00352448_narrowTurnBack = 0.3490658f;
      constexpr float kWideScale = 0.5f; // inline `lui $at, 0x3f00`
      constexpr float kDAT_0035244c_wideTurn = 1.047197f; // +60 degrees
      constexpr float kDAT_00352450_wideTurnBack = 1.047197f;
      constexpr int kLastRetry = 4;

      // puVar11[0x55] / [0x56]: the request's heading and length, taken once
      // before the loop and never recomputed.
      const float requestHeading = std::atan2(entity.desiredDeltaZ34, entity.desiredDeltaX30);
      const float requestSpeed = std::sqrt(entity.desiredDeltaX30 * entity.desiredDeltaX30 +
                                           entity.desiredDeltaZ34 * entity.desiredDeltaZ34);

      float stepX = entity.desiredDeltaX30;
      float stepZ = entity.desiredDeltaZ34;
      float retrySpeed = requestSpeed;
      int attempt = 0;

      for (;;)
      {
        auto destination = sampleAt(startX + stepX, startZ + stepZ);
        if (walkable(destination))
        {
          entity.positionX20 = startX + stepX;
          entity.positionZ24 = startZ + stepZ;
          surface = destination;
          break;
        }

        entity.collisionFlags0c |= 0x0002u;

        // FUN_002262c0:0x00226b00. +0x04 bit 2 is what admits an actor to the
        // ladder at all; without it the refusal stands as it is.
        if ((entity.halfword04 & 0x0004u) == 0 || attempt > kLastRetry)
        {
          break;
        }

        float heading = requestHeading;
        switch (attempt)
        {
        case 0:
          retrySpeed = requestSpeed * kDAT_0035243c_firstRetryScale;
          break;
        case 1:
          retrySpeed = requestSpeed * kDAT_00352440_narrowScale;
          heading += kDAT_00352444_narrowTurn;
          break;
        case 2:
          heading -= kDAT_00352448_narrowTurnBack;
          break;
        case 3:
          retrySpeed = requestSpeed * kWideScale;
          heading += kDAT_0035244c_wideTurn;
          break;
        default:
          heading -= kDAT_00352450_wideTurnBack;
          break;
        }
        ++attempt;

        stepX = retrySpeed * std::cos(heading);
        stepZ = retrySpeed * std::sin(heading);
        if (stepX == 0.0f && stepZ == 0.0f)
        {
          break;
        }

        // `puVar11[0x4b] = puVar11[0x4b] & 0xffff7ffd | 0x4000` -- the refusal is
        // provisional until the ladder runs out, so bit 1 comes back down and
        // 0x4000 marks an actor working its way around something.
        entity.collisionFlags0c = (entity.collisionFlags0c & 0xFFFF7FFDu) | 0x4000u;
      }

      if (!surface.has_value())
      {
        // Nothing was walkable: stay put and re-sample where we already were,
        // so +0x4C and the terrain words still describe the actor's own spot.
        entity.positionX20 = startX;
        entity.positionZ24 = startZ;
        surface = sampleAt(startX, startZ);
        if (!walkable(surface))
        {
          surface.reset();
        }
      }

      if (surface.has_value())
      {
        entity.groundHeight4c = surface->height;
        // The same publish FUN_002262c0 does for the player. Non-player actors
        // need it too: a type 0x62 clone's target is its *leader*, and the chase
        // state gates on the target's +0x6C, so without this the clones sat
        // still. The EE dump has all six enemies reading 0x30010000 here.
        entity.flagWord6c = surface->terrainFlags;
        entity.flagWord70 = surface->terrainFlagsAll;
        entity.groundPrimitive0a = static_cast<std::int16_t>(surface->primitiveIndex);

        // FUN_00227070:133-138 only publishes the corner heights on the
        // four-corner path, so the single-point case leaves them alone.
        if (surface->sampledFourCorners)
        {
          entity.cornerHeight84 = surface->cornerHeights[0];
          entity.cornerHeight88 = surface->cornerHeights[1];
          entity.cornerHeight8c = surface->cornerHeights[2];
          entity.cornerHeight90 = surface->cornerHeights[3];
        }
      }
    }

    // FUN_002262c0:481-520, the vertical settle. Rising and falling are not
    // symmetric: a rise has to clear the headroom test and is rolled back
    // whole if it does not, while a fall is simply clamped at the cached
    // ground -- which is also the only thing that raises +0x28 for a
    // stationary actor. That clamp is what lifts Magnus onto the bed: his 0x55
    // wrote -1.200 into +0x4C while leaving him at the -1.500 the script
    // authored, and the next frame snaps him up.
    const float verticalDelta = entity.desiredDeltaY38;
    if (verticalDelta > 0.0f)
    {
      const float before = entity.positionY28;
      entity.positionY28 = before + verticalDelta;
      entity.collisionFlags0c |= 0x0008u;
      bool cleared = false;
      if (environment.terrainSurface)
      {
        const auto headroom = environment.terrainSurface(
            entity.positionX20, entity.positionZ24, entity.positionY28, entity.height58,
            entity.radius54, entity.halfword04, entity.rejectTerrainMask74);
        cleared = headroom.has_value() && headroom->height <= entity.positionY28;
      }
      if (!cleared)
      {
        entity.positionY28 = before;
        entity.verticalVelocity44 = 0.0f;
        entity.collisionFlags0c |= 0x000Cu;
      }
    }
    else
    {
      if (verticalDelta < 0.0f)
      {
        entity.collisionFlags0c |= 0x0010u;
      }
      entity.positionY28 += verticalDelta;
      if (entity.positionY28 <= entity.groundHeight4c)
      {
        entity.positionY28 = entity.groundHeight4c;
        entity.verticalVelocity44 = 0.0f;
        entity.collisionFlags0c |= 0x0005u;
      }
    }

    entity.desiredDeltaX30 = 0.0f;
    entity.desiredDeltaZ34 = 0.0f;
    entity.desiredDeltaY38 = 0.0f;
  }

  // FUN_002d2f40, type 0x28: **build the close-up rig**, once.
  //
  // Type 0x28 is not a character -- it is a mount point. The first time it
  // ticks it allocates three entities and hangs them off each other:
  //
  //   b = FUN_00265e28(0x26);  b->+0x192 = this;  b->+0x194 =  role1(this)
  //   c = FUN_00265e28(0x19);  c->+0x192 = b;     c->+0x194 = -role2(b)
  //   a = FUN_00265e28(0x27);  a->+0x192 = b;     a->+0x194 =  role1(b)
  //   this->+0x198 = a;  this->+0x19C = b;  this->+0x1A0 = c;  this->+0x94 = 1;
  //
  // So `0x26` is the close-up bust -- face, torso and arms -- `0x27` is the
  // **hair**, and `0x19` is a bandana of its own, the same type the field
  // player wears. (Confirmed by capture: hiding the 0x27 slot removes the hair
  // and leaves a headbanded, bald bust behind.) The negated bone index on the cloth is
  // deliberate: it selects FUN_0020cdc0's middle, position-only branch rather
  // than the rigid one, which is what lets the rope hang instead of being
  // welded to the bone's orientation.
  //
  // The allocation order in the original is 0x27, 0x26, 0x19 -- the pool slots
  // come out in that order -- but the *linking* order is 0x26 first, because the
  // other two need its role bones. Both are reproduced.
  //
  // Without this the scene's close-up shot has no body, no head and no cloth,
  // and the field player's own bandana is the only one left -- still parented to
  // pool slot 0, still standing wherever the field model was left.
  void FUN_002d2f40_build_closeup_rig(OriginalEntity &entity,
                                      std::size_t slot,
                                      const ActorEnvironment &environment)
  {
    if (entity.spawnParam94 != 0 || environment.entityPool == nullptr ||
        environment.descriptors == nullptr)
    {
      return;
    }

    EntityPool &pool = *environment.entityPool;
    const std::size_t hair = pool.FUN_00265e28_allocate_and_initialize(0x27, *environment.descriptors);
    const std::size_t body = pool.FUN_00265e28_allocate_and_initialize(0x26, *environment.descriptors);
    const std::size_t cloth = pool.FUN_00265e28_allocate_and_initialize(0x19, *environment.descriptors);

    // FUN_0026bfc0 on any of the three failing. The original reports and keeps
    // going with whatever it got; there is nothing sensible to do either way, so
    // the port takes the same shape rather than half-building the rig.
    if (hair >= pool.slotCount() || body >= pool.slotCount() || cloth >= pool.slotCount())
    {
      return;
    }

    const auto boneForRole = [&](std::size_t of, std::uint8_t role) -> int {
      return environment.FUN_0020dd78_bone_for_role
                 ? static_cast<int>(environment.FUN_0020dd78_bone_for_role(of, role))
                 : 0;
    };

    OriginalEntity &bodyEntity = pool.slot(body);
    bodyEntity.parentSlot192 = static_cast<std::int16_t>(slot);
    bodyEntity.attachBone194 = static_cast<std::int8_t>(boneForRole(slot, 1));

    OriginalEntity &clothEntity = pool.slot(cloth);
    clothEntity.parentSlot192 = static_cast<std::int16_t>(body);
    clothEntity.attachBone194 = static_cast<std::int8_t>(-boneForRole(body, 2));

    OriginalEntity &hairEntity = pool.slot(hair);
    hairEntity.parentSlot192 = static_cast<std::int16_t>(body);
    hairEntity.attachBone194 = static_cast<std::int8_t>(boneForRole(body, 1));

    entity.rigHair198 = static_cast<std::int32_t>(hair);
    entity.rigBust19c = static_cast<std::int32_t>(body);
    entity.rigCloth1a0 = static_cast<std::int32_t>(cloth);
    entity.spawnParam94 = 1;
  }

  // FUN_00265ec0: release a pool slot the way the original does, rather than
  // the way `EntityPool::releaseSlot` does.
  //
  // The pool's own release is the map-load clear: it blanks the slot and stops.
  // FUN_00265ec0 does three more things first, and one of them matters here --
  // FUN_00266098 gives the entity's DAT_00343888 light slot back by writing its
  // radius to 0, which is the *only* thing that frees it. Without that, every
  // sword swing would leave a two-unit white light burning where it ended, and
  // after sixteen swings the table would be full.
  //
  // FUN_00265f70, the second, cascades to anything attached to this entity.
  // FUN_0020e7e0, the third, releases eight sound handles at +0xB0..+0xB8; the
  // port's sound path holds no per-entity handles, so there is nothing to free.
  void FUN_00265ec0_destroy_entity(std::size_t slot, const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;

    // FUN_00266098.
    const std::int8_t lightSlot = pool.slot(slot).lightSlot195;
    if (lightSlot >= 0 && environment.DAT_00343888_lights != nullptr)
    {
      environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(lightSlot)).radius = 0.0f;
    }
    pool.slot(slot).lightSlot195 = -1;

    // FUN_00265f70: everything whose +0x192 names this slot goes with it.
    for (std::size_t child = 0; child < kEntitySlotCount; ++child)
    {
      if (child == slot || pool.status(child) != SlotStatus::ScriptSpawned)
      {
        continue;
      }
      if (pool.slot(child).parentSlot192 == static_cast<std::int16_t>(slot))
      {
        pool.releaseSlot(child);
      }
    }

    pool.releaseSlot(slot);
  }

  // FUN_00256130's spawn block, reached on the frame the swing's timeline
  // cursor first lands on entry 1. Type 0x42 is the blade: grp_0179, four bones
  // and three animations, drawn attached to the swinging entity's role-5 bone.
  //
  // Two things about it are worth stating, because neither is guessable from
  // the entity alone:
  //
  //   The bone is role **5**, not role 4. Role 4 is the right hand, where a
  //   held weapon goes; role 5 on grp_0001 is bone 17, one of the fingers. The
  //   blade is a glow that grows out of the fist, not a sword model in it.
  //
  //   +0x158 is set to pi. The blade model is authored pointing the other way,
  //   and this is the roll that turns it round -- it is not a facing, which is
  //   copied separately into +0x5C.
  std::int32_t FUN_00256130_spawn_sword_effect(const OriginalEntity &owner,
                                               std::size_t ownerSlot,
                                               const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr || environment.descriptors == nullptr)
    {
      return -1;
    }
    EntityPool &pool = *environment.entityPool;
    const std::size_t slot = pool.FUN_00265e28_allocate_and_initialize(
        orphen::ported::player::kSwordEffectTypeId, *environment.descriptors);
    if (slot >= kEntitySlotCount)
    {
      return -1;
    }

    OriginalEntity &effect = pool.slot(slot);
    effect.animationA0 = 1;
    effect.parentSlot192 = static_cast<std::int16_t>(ownerSlot);
    effect.attachBone194 = static_cast<std::int8_t>(
        environment.FUN_0020dd78_bone_for_role
            ? environment.FUN_0020dd78_bone_for_role(ownerSlot, 5)
            : 0);
    effect.attackPower12c = owner.attackPower12c;
    effect.facingRadians5c = owner.facingRadians5c;
    effect.fadeLevel134 = 4;
    effect.rotationY158 = kDAT_00352998_bladeRoll;
    effect.fadeRamp62 = kBladeGlowStart;

    // FUN_00266050: the light allocator that can hand out slots 0..2, so the
    // blade can become a real directional light on a character rather than the
    // flat tint slots 3 and up get. -1 when all sixteen are taken, which the
    // original carries on from -- the swing still happens, unlit.
    effect.lightSlot195 = -1;
    if (environment.DAT_00343888_lights != nullptr)
    {
      const std::int32_t lightSlot =
          environment.DAT_00343888_lights->FUN_00266050_allocateFromZero();
      effect.lightSlot195 = static_cast<std::int8_t>(lightSlot);
      if (lightSlot >= 0)
      {
        auto &light = environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(lightSlot));
        // 0x01000000 stored over +0x0C..+0x0F: colour black, alpha 1. The
        // colour is overwritten from the glow ramp on this same frame; what
        // this store is really for is the radius below, which is what makes the
        // slot live.
        light.red = 0;
        light.green = 0;
        light.blue = 0;
        light.alpha = 1;
        light.radius = kBladeLightRadius;
        environment.DAT_00343888_lights->noteRadius(static_cast<std::uint32_t>(lightSlot),
                                                    kBladeLightRadius);
      }
    }

    // FUN_00216078(ownerType, 0, effect + 0x198): record 0 of the *swinger's*
    // type, not the blade's. For the lead player that is `01 00 1e 00` --
    // element 0, +30% power, reaction 0. FUN_002148a8 is its only reader.
    //
    // The original leaves +0x198 as it was when the type has no record; so does
    // this, and a blade with a zero record deals the floor of one point.
    if (environment.DAT_00354d6c_hitParameters != nullptr)
    {
      const auto record =
          environment.DAT_00354d6c_hitParameters->FUN_00216078_record(owner.typeId00, 0);
      if (record.has_value())
      {
        effect.hitParameters198 = record->packed();
      }
    }

    return static_cast<std::int32_t>(slot);
  }

  // FUN_002d21b8, type 0x42: the blade's own frame.
  //
  // Three jobs, and it does all of them every frame:
  //
  //   drive the DAT_00343888 light slot from the blade's own bone 0, ramping
  //   the colour from 128 grey to white as +0x62 climbs 0x1000 -> 0x1FE0;
  //
  //   step the fade level at +0x134 by 4 a frame until it passes 0x78, then
  //   drop it to 0 -- so the blade materialises over about thirty frames and
  //   then draws solid;
  //
  //   and check that the lead player is still in state 0x1C, deleting itself
  //   the moment it is not. That last test is read straight off DAT_0058BF10,
  //   pool slot 0's +0x60, which is why the blade cannot outlive the swing even
  //   if its own animation has not finished.
  //
  // The animation is the other half of its lifetime: animation 1 is the swing,
  // and on the frame it completes FUN_00225bc8 puts it on animation 0 (the hit
  // test's idle). Animation 2 is the dissipate FUN_00256130 selects when the
  // swing's keyframe event fires, and completing *that* deletes the blade.
  //
  // The fourth job is the one that makes the swing mean anything: on animations
  // 0 and 1 the blade runs FUN_002148a8, the swept hit test, against the whole
  // pool. FUN_002d59c0 -- `FUN_0023bbd8(0, 3)` -- is the reaction it would
  // trigger, but FUN_002148a8's return value is unreachable-dead in this build
  // and always zero, so the call site is written the original's way and never
  // fires. See ported/entity/original_hit_test.cpp.
  void FUN_002d21b8_sword_effect(OriginalEntity &effect,
                                 std::size_t slot,
                                 const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;

    const std::int16_t animation = static_cast<std::int16_t>(effect.animationA0);
    const bool animationComplete = (effect.flags06 & kAnimationComplete06) != 0;

    // FUN_002d21b8's control flow: the hit test runs on animation 0 and on
    // animation 1, but *not* on the frame animation 1 completes -- that frame
    // falls through to the animation-0 switch and jumps straight to the light
    // block. Animation 2, the dissipate, never tests at all.
    bool runHitTest = false;
    if (animation == 1)
    {
      runHitTest = !animationComplete;
    }
    else if (animation == 0)
    {
      runHitTest = true;
    }
    if (runHitTest && environment.hitTest != nullptr)
    {
      const auto parameters =
          orphen::ported::resource::HitParameters::unpack(effect.hitParameters198);
      const std::int8_t contacts =
          FUN_002148a8_swept_hit_test(effect, slot, parameters, *environment.hitTest);
      if (contacts != 0)
      {
        // FUN_002d59c0, one call: FUN_0023bbd8(0, 3), the hit cue. That is the
        // sound engine's priority-channel entry point rather than
        // FUN_00267d38's, and the port reaches the engine only through the
        // latter -- the same reason the magic launch skips its own
        // FUN_0023bbd8. The branch is here so the cue is one line away once
        // that path exists.
      }
    }

    if (animation == 1)
    {
      if (animationComplete)
      {
        // FUN_00225bc8(effect, 0).
        effect.animationA0 = 0;
        effect.stateResetA4 = 999;
        effect.previousSubstateA2 = 0xffff;
        effect.flags06 = static_cast<std::uint16_t>(effect.flags06 & 0xff38);
        effect.timelineCursorA8 = 0;
      }
    }
    else if (animation == 2 && animationComplete)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
      return;
    }

    const std::int8_t lightSlot = effect.lightSlot195;
    if (lightSlot >= 0 && environment.DAT_00343888_lights != nullptr)
    {
      auto &light = environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(lightSlot));
      if (environment.FUN_0020dc88_bone_point)
      {
        // DAT_003266f8, read out of the EE dump: (0, 0, 0.65), up bone 0.
        const orphen::ported::psm2::Vec3 point =
            environment.FUN_0020dc88_bone_point(slot, 0, {0.0f, 0.0f, 0.649999976f});
        light.x = point.x;
        light.y = point.y;
        light.z = point.z;
      }

      // `(v + 0x1F) >> 5` for negative v, `v >> 5` otherwise: a signed divide
      // by 32. 0x1000 gives 128 and 0x1FE0 gives 255.
      const std::int16_t glow = static_cast<std::int16_t>(effect.fadeRamp62);
      const std::uint8_t level =
          static_cast<std::uint8_t>((glow < 0 ? (glow + 0x1f) : glow) >> 5);
      light.red = level;
      light.green = level;
      light.blue = level;

      if (glow < static_cast<std::int16_t>(kBladeGlowEnd))
      {
        const std::int16_t stepped = static_cast<std::int16_t>(
            effect.fadeRamp62 + static_cast<std::uint16_t>(environment.frameTicks * 8u));
        effect.fadeRamp62 = static_cast<std::uint16_t>(stepped);
        if (stepped > static_cast<std::int16_t>(kBladeGlowEnd - 1))
        {
          effect.fadeRamp62 = kBladeGlowEnd;
        }
      }
    }

    // +0x134 is the draw's alpha over 128, and 0 means opaque -- so this ramps
    // the blade in from 4/128 and then, past 0x78, snaps it to fully solid.
    // Note the original's tick is a flat 4, not scaled by the frame time.
    if (effect.fadeLevel134 != 0)
    {
      const std::uint8_t stepped = static_cast<std::uint8_t>(effect.fadeLevel134 + 4);
      effect.fadeLevel134 = stepped > 0x78 ? 0 : stepped;
    }

    // DAT_0058bf10, pool slot 0's +0x60.
    if (pool.leadPlayer().state60 != orphen::ported::player::kStateSwordAttack)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
    }
  }

  // FUN_00229ef0: set an entity's size scale and rescale its collision volume
  // from its descriptor. The two size fields are *derived*, not stored, so
  // writing +0x14C without this leaves the radius and height at their spawn
  // values and the entity grows only visually.
  void FUN_00229ef0_set_scale(OriginalEntity &entity, float scale,
                              const EntityDescriptorTable *descriptors)
  {
    entity.scale14c = scale;
    entity.scaleZ150 = scale;
    if (descriptors == nullptr)
    {
      return;
    }
    const auto descriptor =
        descriptors->FUN_00229980_resolve(static_cast<std::uint32_t>(entity.effectiveTypeId()));
    if (descriptor.has_value())
    {
      entity.radius54 = scale * descriptor->radius0x08;
      entity.height58 = scale * descriptor->height0x0c;
      // The hit-test volume at +0x11C/+0x120 comes off the same two lines.
      entity.hitVolumeRadius11c = entity.radius54;
      entity.hitVolumeHeight120 = entity.height58;
    }
  }

  // FUN_002d2ca8: pick what the magic projectile will chase.
  //
  // One pass over pool slots 10 upward -- **not** slot 0, so it can never lock
  // onto the caster -- taking the nearest candidate inside ten units. A
  // candidate is any live entity whose +0x02 has bit 0x08 and whose +0x04 does
  // not have bit 0x10; on type 0x62 that pair means "an enemy that is not
  // already dying".
  //
  // The elevation gate is the interesting half. A candidate more than two units
  // away vertically is taken on distance alone, but one *within* two units has
  // to also sit inside a 60-degree cone (fGpffffa738 = 1.0472 rad) measured
  // from the projectile to the candidate's waist. Read the branch carefully:
  // the far-in-height case skips the cone test rather than failing it.
  //
  // The choice is made once, at spawn, and never revisited -- so the projectile
  // locks on and stays locked.
  std::int32_t FUN_002d2ca8_find_homing_target(const ActorEnvironment &environment,
                                               const OriginalEntity &projectile)
  {
    if (environment.entityPool == nullptr)
    {
      return -1;
    }
    const EntityPool &pool = *environment.entityPool;

    std::int32_t best = -1;
    float bestDistance = kDAT_002d2ca8_searchRadius;

    for (std::size_t slot = kFirstScriptSlot; slot < kEntitySlotCount; ++slot)
    {
      if (pool.status(slot) != SlotStatus::ScriptSpawned)
      {
        continue;
      }
      const OriginalEntity &candidate = pool.slot(slot);
      if ((candidate.descriptorFlags02 & 0x0008u) == 0 ||
          (candidate.halfword04 & 0x0010u) != 0)
      {
        continue;
      }

      // FUN_0023a4e8: horizontal distance only.
      const float dx = candidate.positionX20 - projectile.positionX20;
      const float dz = candidate.positionZ24 - projectile.positionZ24;
      const float distance = std::sqrt(dx * dx + dz * dz);
      if (distance >= bestDistance)
      {
        continue;
      }

      const float heightDelta = candidate.positionY28 - projectile.positionY28;
      if (std::fabs(heightDelta) < 2.0f)
      {
        // FUN_00305408 is atan2. The waist, not the feet.
        const float elevation = std::atan2(
            (candidate.positionY28 + candidate.height58 * 0.5f) - projectile.positionY28, distance);
        if (std::fabs(elevation) >= kfGpffffa738_elevationCone)
        {
          continue;
        }
      }

      bestDistance = distance;
      best = static_cast<std::int32_t>(slot);
    }

    return best;
  }

  // FUN_002d2e00: spawn the magic projectile, type 0x44, at a world point.
  //
  // The point is the caster's role-4 bone -- the right hand, where a held
  // weapon goes -- plus DAT_0031e0a8, and the spawn is *refused* if the floor
  // under that point is above it. That is the only guard, and it is what stops
  // a cast started while clipping into geometry putting a projectile inside the
  // world.
  //
  // Two flags go in at +0x04: bit 0x02 puts the ground query on the
  // single-point path, and bit 0x100 turns physics off entirely, which is what
  // lets FUN_002562b0 hold the projectile in the caster's hand by writing +0x20
  // directly. The launch clears 0x100 and the projectile starts flying.
  std::int32_t FUN_002d2e00_spawn_magic_projectile(const OriginalEntity &owner,
                                                   const orphen::ported::psm2::Vec3 &handPoint,
                                                   const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr || environment.descriptors == nullptr ||
        !environment.FUN_00227798_probe)
    {
      return -1;
    }

    // FUN_00227798 at the hand. `groundHeight <= handHeight` or nothing spawns.
    const float groundHeight =
        environment.FUN_00227798_probe(handPoint.x, handPoint.y, handPoint.z).height;
    if (groundHeight > handPoint.z)
    {
      return -1;
    }

    EntityPool &pool = *environment.entityPool;
    const std::size_t slot = pool.FUN_00265e28_allocate_and_initialize(
        orphen::ported::player::kMagicProjectileTypeId, *environment.descriptors);
    if (slot >= kEntitySlotCount)
    {
      return -1;
    }

    OriginalEntity &projectile = pool.slot(slot);
    projectile.positionX20 = handPoint.x;
    projectile.positionZ24 = handPoint.y;
    projectile.positionY28 = handPoint.z;
    projectile.groundHeight4c = groundHeight;
    projectile.facingRadians5c = owner.facingRadians5c;
    projectile.rejectTerrainMask74 = 0;
    projectile.requiredTerrainMask78 = 0;
    projectile.halfword04 = static_cast<std::uint16_t>(projectile.halfword04 | 0x0102u);
    // uGpffffa73c, 0.01, written over the radius and the height *and* their two
    // mirrors at +0x11C/+0x120. FUN_00229ef0 rebuilds them from the descriptor
    // every time the scale changes, so this is only the starting size.
    projectile.radius54 = kuGpffffa73c_projectileExtent;
    projectile.height58 = kuGpffffa73c_projectileExtent;
    projectile.hitVolumeRadius11c = kuGpffffa73c_projectileExtent;
    projectile.hitVolumeHeight120 = kuGpffffa73c_projectileExtent;
    projectile.projectileSpeed19c = kuGpffffa740_projectileSpeed;

    // FUN_00266050 again -- the projectile carries its own light the whole way,
    // and the trail ghosts it drops do not. Colour 0x00040404, almost black:
    // state 0's charge ramp is what brings it up.
    projectile.lightSlot195 = -1;
    if (environment.DAT_00343888_lights != nullptr)
    {
      const std::int32_t lightSlot =
          environment.DAT_00343888_lights->FUN_00266050_allocateFromZero();
      projectile.lightSlot195 = static_cast<std::int8_t>(lightSlot);
      if (lightSlot >= 0)
      {
        auto &light = environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(lightSlot));
        light.red = 4;
        light.green = 4;
        light.blue = 4;
        light.alpha = 0;
        light.radius = kProjectileLightRadius;
        environment.DAT_00343888_lights->noteRadius(static_cast<std::uint32_t>(lightSlot),
                                                    kProjectileLightRadius);
        // FUN_002660d0: put the light where the projectile already is, before
        // anything gets a chance to draw it at the origin.
        light.x = projectile.positionX20;
        light.y = projectile.positionZ24;
        light.z = projectile.positionY28;
      }
    }

    projectile.homingTarget198 = FUN_002d2ca8_find_homing_target(environment, projectile);
    projectile.hitCooldown1aa = kProjectileHitCooldown;
    projectile.homingTimer1a8 = kProjectileHomingTicks;

    // FUN_00216078(casterType, 1, projectile + 0x1AC): record *1* of the
    // caster's type, where the sword blade takes record 0. FUN_00215ac8 is its
    // only reader.
    if (environment.DAT_00354d6c_hitParameters != nullptr)
    {
      const auto record =
          environment.DAT_00354d6c_hitParameters->FUN_00216078_record(owner.typeId00, 1);
      if (record.has_value())
      {
        projectile.hitParameters1ac = record->packed();
      }
    }

    return static_cast<std::int32_t>(slot);
  }

  // FUN_002d2470, type 0x44: the magic projectile's own frame.
  //
  // Four states on +0x60, and they are a lifetime rather than a machine -- each
  // one only ever moves forward:
  //
  //   0  charging in the caster's hand. FUN_002562b0 pins the position; this
  //      grows the scale to 2.0 by a tenth a frame, brightens the light by 4 a
  //      frame, and orbits a hair. It deletes itself the moment the caster
  //      leaves state 0x1D, so an interrupted cast takes the charge with it.
  //   1  flying. Homing, then movement, then a trail ghost every other frame.
  //   2  a trail ghost: shrink by 0.05 a frame and die when the animation ends.
  //   4  the impact. Fade the light down by 0x20202 a frame and delete.
  //
  // The homing is two angles. The yaw at +0x5C and the elevation at +0x1A0 each
  // step toward the target through FUN_0023a320, capped by +0x1A4 -- which
  // starts at zero and ramps by 0.005 a frame to 0.349. So the projectile
  // leaves the hand travelling dead straight and only tightens later, which is
  // most of why it reads as a guided missile rather than a tracking beam.
  //
  // The hit test is FUN_00215ac8, the plain box form: a 0.15 cube in the
  // horizontal plane running from the projectile's feet to its full height, run
  // once the cooldown at +0x1AA expires. Its contact count is the third of the
  // three things that detonate the bolt, and the only one that ever fires on
  // something alive.
  void FUN_002d2470_magic_projectile(OriginalEntity &projectile,
                                     std::size_t slot,
                                     const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;

    // FUN_0023a068, and the original discards the result -- so the projectile
    // ticks its freeze timer but is not gated on it.
    FUN_0023a068_freeze_gate(projectile, environment.frameTicks);

    const auto light = [&]() -> orphen::ported::render::LightTable::Slot * {
      if (projectile.lightSlot195 < 0 || environment.DAT_00343888_lights == nullptr)
      {
        return nullptr;
      }
      return &environment.DAT_00343888_lights->slot(
          static_cast<std::uint32_t>(projectile.lightSlot195));
    };

    // ---- state 0: charging in the hand ------------------------------------
    if (projectile.state60 == 0)
    {
      // DAT_0058bf10 again, pool slot 0's +0x60.
      if (pool.leadPlayer().state60 != orphen::ported::player::kStateMagicCast)
      {
        FUN_00265ec0_destroy_entity(slot, environment);
        return;
      }

      // A movement request the physics never spends -- +0x04 bit 0x100 is on
      // for the whole of this state. It is here because the launch clears that
      // bit, and whatever has piled up is the projectile's first push.
      const float orbit = projectile.facingRadians5c + kDAT_0035467c_chargeOrbitBias;
      projectile.desiredDeltaX30 += std::cos(orbit) * kDAT_00354680_chargeOrbitStep;
      projectile.desiredDeltaZ34 += std::sin(orbit) * kDAT_00354680_chargeOrbitStep;

      if (projectile.scale14c < 2.0f)
      {
        FUN_00229ef0_set_scale(projectile, projectile.scale14c + kDAT_00354684_chargeGrowth,
                               environment.descriptors);
      }

      if (auto *slotLight = light())
      {
        const int level = std::min(static_cast<int>(slotLight->red) + 4, 0xFF);
        slotLight->red = static_cast<std::uint8_t>(level);
        slotLight->green = static_cast<std::uint8_t>(level);
        slotLight->blue = static_cast<std::uint8_t>(level);
      }
      return;
    }

    // ---- state 2: a trail ghost -------------------------------------------
    if (projectile.state60 == 2)
    {
      if ((projectile.flags06 & kAnimationComplete06) != 0)
      {
        FUN_00265ec0_destroy_entity(slot, environment);
        return;
      }
      FUN_00229ef0_set_scale(projectile, projectile.scale14c - kDAT_00354688_trailShrink,
                             environment.descriptors);
      return;
    }

    // ---- state 4: the impact ----------------------------------------------
    if (projectile.state60 == 4)
    {
      auto *slotLight = light();
      if (slotLight == nullptr)
      {
        FUN_00265ec0_destroy_entity(slot, environment);
        return;
      }
      // The original compares and decrements the packed rgb word at +0x0C as a
      // single u32 (`0x80808 < w`, then `w -= 0x20202`), which is only the same
      // as three independent ramps because the three bytes stay equal. They do:
      // nothing ever writes them apart.
      if (slotLight->red > 0x08)
      {
        slotLight->red = static_cast<std::uint8_t>(slotLight->red - 2);
        slotLight->green = static_cast<std::uint8_t>(slotLight->green - 2);
        slotLight->blue = static_cast<std::uint8_t>(slotLight->blue - 2);
        return;
      }
      FUN_00265ec0_destroy_entity(slot, environment);
      return;
    }

    // ---- state 1: flying ---------------------------------------------------

    // +0x1AA, the hit-test cooldown. The test and the countdown are the two
    // halves of one `if`: while the cooldown is running nothing is tested, and
    // the frame it reaches zero is the first the box is measured.
    std::int8_t contacts = 0;
    if (projectile.hitCooldown1aa == 0)
    {
      // DAT_0035468c, 0.15: a cube in the horizontal plane, and vertically the
      // projectile's own +0x28 to +0x28 + +0x58. Not centred on it -- the box
      // sits on its feet.
      const std::array<float, 6> box{
          projectile.positionX20 - kDAT_0035468c_hitExtent,
          projectile.positionX20 + kDAT_0035468c_hitExtent,
          projectile.positionZ24 - kDAT_0035468c_hitExtent,
          projectile.positionZ24 + kDAT_0035468c_hitExtent,
          projectile.positionY28,
          projectile.positionY28 + projectile.height58};
      if (environment.hitTest != nullptr)
      {
        const auto parameters =
            orphen::ported::resource::HitParameters::unpack(projectile.hitParameters1ac);
        contacts = FUN_00215ac8_box_hit_test(projectile, slot, box, parameters,
                                             *environment.hitTest);
      }
    }
    else
    {
      const std::int16_t remaining = static_cast<std::int16_t>(
          projectile.hitCooldown1aa - static_cast<std::uint16_t>(environment.frameTicks));
      projectile.hitCooldown1aa = static_cast<std::uint16_t>(remaining < 0 ? 0 : remaining);
    }

    projectile.fadeRamp62 =
        static_cast<std::uint16_t>(projectile.fadeRamp62 + static_cast<std::uint16_t>(environment.frameTicks));
    const std::int16_t lifetime = static_cast<std::int16_t>(projectile.fadeRamp62);

    // +0x04 bit 0 disables the entity-vs-entity clamp. The projectile carries it
    // for its first 0x140 ticks -- ten frames -- so it can leave the caster.
    if ((projectile.halfword04 & 0x0001u) != 0 && lifetime > 0x13f)
    {
      projectile.halfword04 = static_cast<std::uint16_t>(projectile.halfword04 & 0xfffeu);
    }

    // Detonate: hit something solid, outlived 0x2580 ticks (five seconds), or
    // the box test above touched something.
    //
    // The wall case still does not fire. `integrateNonPlayerMovement` is not
    // FUN_002262c0 -- see the note on it -- so a non-player actor passes through
    // geometry and +0x0C never picks up the blocked bits.
    if ((projectile.collisionFlags0c & 0x0266u) != 0 || lifetime > kProjectileMaxLifetime ||
        contacts != 0)
    {
      // 0x002d2818: up to a hundred particles into DAT_00355620, fanning out
      // from the projectile's own facing, and FUN_002d2348 installed as their
      // stepper. The burst happens before the state change, and it reads the
      // projectile's position and facing, so it has to come first.
      if (environment.FUN_002d2470_spawn_impact_burst)
      {
        environment.FUN_002d2470_spawn_impact_burst(projectile, slot);
      }
      projectile.state60 = 4;
      projectile.halfword08 = static_cast<std::uint16_t>(projectile.halfword08 | 0x0001u);
      return;
    }

    // Homing, while +0x1A8 has ticks left.
    if (projectile.homingTimer1a8 != 0)
    {
      const float turnRate =
          std::min(projectile.projectileTurnRate1a4 + kDAT_003546a0_turnRamp, kDAT_003546a4_turnMax);
      projectile.projectileTurnRate1a4 = turnRate;

      const std::int32_t target = projectile.homingTarget198;
      bool turned = false;
      if (target >= 0 && static_cast<std::size_t>(target) < kEntitySlotCount &&
          (pool.slot(static_cast<std::size_t>(target)).halfword04 & 0x0010u) == 0)
      {
        const OriginalEntity &chased = pool.slot(static_cast<std::size_t>(target));
        const float dx = chased.positionX20 - projectile.positionX20;
        const float dz = chased.positionZ24 - projectile.positionZ24;

        // FUN_0023a4b8 is atan2 of the horizontal offset; FUN_0023a320 steps
        // toward it, capped, and returns 0 once inside the dead zone.
        const float yawStep = FUN_0023a320_approach_angle(
            projectile.facingRadians5c, std::atan2(dz, dx), turnRate);
        if (yawStep != 0.0f)
        {
          projectile.facingRadians5c += yawStep;
        }

        const float distance = std::sqrt(dx * dx + dz * dz);
        const float wantedPitch = std::atan2(
            (chased.positionY28 + chased.height58 * 0.5f) - projectile.positionY28, distance);
        const float pitchStep =
            FUN_0023a320_approach_angle(projectile.projectilePitch1a0, wantedPitch, turnRate);
        if (pitchStep != 0.0f)
        {
          projectile.projectilePitch1a0 += pitchStep;
          turned = true;
        }
      }

      // The original only reaches the timer decrement through the pitch branch's
      // fall-through or one of the two early exits -- every path arrives here,
      // so the timer runs down whether or not there is still a target.
      (void)turned;
      const std::int16_t remaining = static_cast<std::int16_t>(
          projectile.homingTimer1a8 - static_cast<std::uint16_t>(environment.frameTicks));
      projectile.homingTimer1a8 = static_cast<std::uint16_t>(remaining < 0 ? 0 : remaining);
    }

    // Movement. Speed is per tick, so the frame's ticks scale it; the pitch
    // splits it into a vertical part and a horizontal one, and the yaw spreads
    // the horizontal part over X and Z.
    const float step = projectile.projectileSpeed19c * static_cast<float>(environment.frameTicks);
    const float horizontal = step * std::cos(projectile.projectilePitch1a0);
    projectile.desiredDeltaY38 += step * std::sin(projectile.projectilePitch1a0);
    projectile.desiredDeltaX30 += horizontal * std::cos(projectile.facingRadians5c);
    projectile.desiredDeltaZ34 += horizontal * std::sin(projectile.facingRadians5c);

    // FUN_002660d0: the light rides the projectile.
    if (auto *slotLight = light())
    {
      slotLight->x = projectile.positionX20;
      slotLight->y = projectile.positionZ24;
      slotLight->z = projectile.positionY28;
    }

    // Every other frame, a ghost of itself left behind at the current position
    // and scale, in state 2, with physics off so it hangs where it was dropped.
    // This is the trail.
    if ((environment.DAT_003555b4_frameCounter & 1u) != 0 && environment.descriptors != nullptr)
    {
      const std::size_t ghost = pool.FUN_00265e28_allocate_and_initialize(
          projectile.typeId00, *environment.descriptors);
      if (ghost < kEntitySlotCount)
      {
        OriginalEntity &trail = pool.slot(ghost);
        trail.positionX20 = projectile.positionX20;
        trail.positionZ24 = projectile.positionZ24;
        trail.positionY28 = projectile.positionY28;
        trail.groundHeight4c = projectile.groundHeight4c;
        FUN_00229ef0_set_scale(trail, projectile.scale14c, environment.descriptors);
        trail.state60 = 2;
        trail.animationA0 = 1;
        trail.halfword04 = static_cast<std::uint16_t>(trail.halfword04 | 0x0100u);
      }
    }
  }

  // FUN_002e7328 (0x002e7328), the behaviour of type 0x1C7 -- the guard shield.
  // One is spawned per party member by FUN_00242df0 and parked in
  // DAT_0031dabc; FUN_0024cba0 (class-1 state 117) raises it by setting its
  // animation to 1 and stamping the caster's pool slot into +0x94.
  //
  // The handler is the whole visible half of Guard. Three things about it are
  // worth stating, because none is guessable from the state handler alone:
  //
  //   **The shield is not attached, it is re-placed every frame.** There is no
  //   +0x192 parent and no bone: the handler copies the caster's facing and XY
  //   straight across and sits the shield at 75% of the caster's body height
  //   (+0x58). That is why FUN_00242df0 spawns it with +0x192 = -1.
  //
  //   **+0xA0 is a four-step sequence, not a looping animation.** 1 is the
  //   raise (one frame -- it unhides and immediately becomes 0), 0 is the hold,
  //   2 is the close, and 3/4 mean "destroy me on the next finished frame".
  //   Each step advances on +0x06 bit 0, the animation-finished flag.
  //
  //   **The caster's control block is what ends the guard**, not the state
  //   handler. Every frame the shield is not already closing it reads the
  //   caster's current action byte, and anything other than 0x90 -- state 118
  //   writes 6 -- drops it into the close animation. So the shield outlives the
  //   guard state by exactly the length of that animation.
  void FUN_002e7328_guard_shield(OriginalEntity &shield,
                                 std::size_t shieldSlot,
                                 const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    // +0x94 is read as a *signed* char here and as an unsigned one four lines
    // into the control-block lookup below. That is the original's own
    // inconsistency, not a transcription slip; it only bites above slot 127,
    // where the signed read would index behind the pool. Rejected rather than
    // reproduced, because the original's version of it is an out-of-bounds read.
    const std::int8_t casterIndex = static_cast<std::int8_t>(shield.spawnParam94);
    if (casterIndex < 0 || static_cast<std::size_t>(casterIndex) >= kEntitySlotCount)
    {
      return;
    }
    const std::size_t casterSlot = static_cast<std::size_t>(casterIndex);

    shield.depthBias133 = -0x0A;
    shield.halfword04 = static_cast<std::uint16_t>(shield.halfword04 | 0x0010u);

    const std::int16_t animation = static_cast<std::int16_t>(shield.animationA0);
    if (animation < 5 && animation > 2)
    {
      // Steps 3 and 4: gone as soon as the animation reports finished.
      if ((shield.flags06 & 1u) != 0)
      {
        FUN_00265ec0_destroy_entity(shieldSlot, environment);
      }
      return;
    }

    const OriginalEntity &caster = pool.slot(casterSlot);
    shield.facingRadians5c = caster.facingRadians5c;
    shield.positionX20 = caster.positionX20;
    shield.flags06 = static_cast<std::uint16_t>(shield.flags06 & 0xFFEFu);
    shield.positionZ24 = caster.positionZ24;
    const float height = caster.height58 * 0.75f + caster.positionY28;
    shield.groundHeight4c = height;
    shield.positionY28 = height;
    shield.previousGroundHeight50 = height;

    std::int16_t step = static_cast<std::int16_t>(shield.animationA0);
    if (step != 2)
    {
      // The caster's own control block, reached through *its* +0x95 rather than
      // through the shield's -- the shield's +0x94 is a pool slot, +0x95 on that
      // entity is the party member index.
      ActorEnvironment::BattleMemberView view;
      const std::uint32_t member = static_cast<std::uint32_t>(caster.byte95) - 1u;
      const bool haveBlock = caster.byte95 != 0 && environment.DAT_0031d7b0_battleMember &&
                             environment.DAT_0031d7b0_battleMember(member, view);
      // 0x90 is the guard action; 0x0B pending is the one interruption that
      // closes the shield while the action byte still reads as guarding.
      if (!haveBlock || view.currentAction0f != 0x90 || view.pendingAction0e == 0x0B)
      {
        FUN_00225bc8_set_animation(shield, 2);
      }
      step = static_cast<std::int16_t>(shield.animationA0);
    }

    if (step == 1)
    {
      // The raise. This is the only place the shield becomes visible: the spawn
      // hid it with +0x08 bit 0 and nothing in state 117 clears that.
      shield.flags06 = static_cast<std::uint16_t>(shield.flags06 & 0xFFEFu);
      shield.halfword08 = static_cast<std::uint16_t>(shield.halfword08 & 0xFFFEu);
      FUN_00225bc8_set_animation(shield, 0);
      shield.halfword08 = static_cast<std::uint16_t>(shield.halfword08 | 0x0010u);
      return;
    }
    if (step < 2)
    {
      // step 0, the hold. It ends when the hold animation reports finished,
      // which for a looping clip is the frame it wraps.
      if (step != 0 || (shield.flags06 & 1u) == 0)
      {
        return;
      }
      FUN_00225bc8_set_animation(shield, 2);
      shield.flags06 = static_cast<std::uint16_t>(shield.flags06 | 0x0010u);
      shield.halfword08 = static_cast<std::uint16_t>(shield.halfword08 | 1u);
      return;
    }
    if (step != 2)
    {
      return;
    }
    // step 2, the close. Hidden again once it finishes.
    if ((shield.flags06 & 1u) == 0)
    {
      return;
    }
    shield.flags06 = static_cast<std::uint16_t>(shield.flags06 | 0x0010u);
    shield.halfword08 = static_cast<std::uint16_t>(shield.halfword08 | 1u);
  }

  // FUN_002f13d0 (0x002f13d0), the behaviour of type 0x1E3 -- the one shared
  // hit effect FUN_002432d8:331 spawns for the whole battle and parks in
  // DAT_0031dad0. Not in src/; recovered from SLUS_200.11 at 0x002F13D0..
  // 0x002F141C, which is the whole function.
  //
  //     addiu v0, zero, -0x18      entity+0x133 = -24
  //     lh    v1, DAT_00355588
  //     bnel  v1, zero, +          branch-likely: skip the hide when a request
  //     lhu   v0, 8(a0)            is pending
  //     ori   v0, v0, 1            entity+0x08 |= 1
  //     ...
  //
  // `DAT_00355588` is a one-frame request word. FUN_002f1380 -- the "show the
  // hit effect here" setter every damage path calls with a position, a scale
  // and a height -- raises bit 0 in it; this handler is what acts on it, and
  // clears it again. So the effect is *hidden by default* and only becomes
  // visible on the frames a hit asked for it.
  //
  // That default is the whole point of porting this. FUN_002432d8 spawns the
  // entity with no post-spawn writes at all, so it arrives from FUN_00229c40
  // with +0x08 bit 0 clear (drawable), scale 1, and position (0,0,0). Without
  // its behaviour it sits at the world origin as a full-size sprite for the
  // entire battle -- which is exactly what the port drew.
  void FUN_002f13d0_shared_hit_effect(OriginalEntity &entity,
                                      const ActorEnvironment &environment)
  {
    entity.depthBias133 = -0x18;

    std::uint16_t request = environment.DAT_00355588_hitEffectRequest != nullptr
                                ? *environment.DAT_00355588_hitEffectRequest
                                : static_cast<std::uint16_t>(0);
    if (request == 0)
    {
      entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 1u);
    }

    const std::uint16_t flags08 = entity.halfword08;
    if ((flags08 & 1u) == 0)
    {
      // Already showing. The request is consumed either way, so a caller that
      // stops asking lets the next frame's hide branch take it back down.
      if (environment.DAT_00355588_hitEffectRequest != nullptr)
      {
        *environment.DAT_00355588_hitEffectRequest = 0;
      }
      return;
    }

    // Hidden. The animation is rewound every frame it stays hidden, so a
    // request always starts the effect from frame 0.
    entity.animationA0 = 0;
    if (request == 0)
    {
      return;
    }
    entity.halfword08 = static_cast<std::uint16_t>(flags08 & 0xFFFEu);
  }

  // FUN_00248e48 / FUN_00248e58, the battle module's frame timers. Duplicated
  // here rather than reached for: these are the only two things the effect
  // behaviours need out of that module, and both are three lines.
  //
  // FUN_00248e48 is `(frames << 21) >> 16` -- 32 ticks per frame, through a
  // 16-bit truncation the caller relies on. FUN_00248e58 subtracts a frame and
  // clamps at zero through an unsigned wrap test rather than a comparison.
  constexpr std::int16_t FUN_00248e48_arm_timer(std::int32_t frames)
  {
    const std::uint32_t shifted = static_cast<std::uint32_t>(frames) << 21;
    return static_cast<std::int16_t>(static_cast<std::int32_t>(shifted) >> 16);
  }

  constexpr std::uint16_t FUN_00248e58_step_timer(std::uint16_t value, std::uint16_t frameTicks)
  {
    if (value == 0)
    {
      return 0;
    }
    const std::uint16_t stepped = static_cast<std::uint16_t>(value - frameTicks);
    return (value < stepped) ? std::uint16_t{0} : stepped;
  }

  // FUN_002d9c88 (0x002d9c88), the behaviour of type 0x18F (399) -- the ring on
  // the ground under a battle character. FUN_00242df0 spawns one per party
  // member and parks it in DAT_0031da8c.
  //
  // It is a ring *and* a ring factory: at animation 0 it periodically spawns
  // copies of itself, and a copy is told "you are a pulse, shrink and die" by
  // being handed a +0x198 that names another marker instead of a character.
  // The test is `caster +0x95 == 0` -- a marker is not a party member, so it
  // has no member index, and that single byte is the whole distinction.
  //
  // It also carries the charge: FUN_002d9b78 writes the accumulator into +0x19A
  // and scales the ring by it, and +0x19A being non-zero is what lets the
  // pulses fire at all. That is why the ring swells while a spell is held.
  void FUN_002d9c88_cast_marker(OriginalEntity &marker,
                                std::size_t slot,
                                const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    const std::size_t casterSlot = marker.markerCaster198;
    if (casterSlot >= kEntitySlotCount)
    {
      return;
    }
    const OriginalEntity &caster = pool.slot(casterSlot);

    marker.depthBias133 = -0x0C;
    marker.fadeLevel134 = 0;
    marker.positionX20 = caster.positionX20;
    marker.positionZ24 = caster.positionZ24;
    marker.positionY28 = caster.positionY28;
    marker.groundHeight4c = caster.positionY28;
    marker.previousGroundHeight50 = caster.positionY28;

    if (caster.byte95 == 0)
    {
      // A pulse. It rides another marker, shrinks toward nothing, and is
      // destroyed the moment either axis passes below 0.2.
      const float ticks = static_cast<float>(environment.frameTicks);
      const float sx = marker.scale14c - (marker.scale14c / 1280.0f) * ticks;
      const float sz = marker.scaleZ150 - (marker.scaleZ150 / 1280.0f) * ticks;
      marker.scale14c = sx;
      marker.scaleZ150 = sz;
      if (sx < 0.2f || sz < 0.2f) // fGpffffa8b8
      {
        FUN_00265ec0_destroy_entity(slot, environment);
      }
      return;
    }

    ActorEnvironment::BattleMemberView view;
    const std::uint32_t member = static_cast<std::uint32_t>(caster.byte95) - 1u;
    const bool haveBlock = environment.DAT_0031d7b0_battleMember &&
                           environment.DAT_0031d7b0_battleMember(member, view);
    const std::uint8_t action = haveBlock ? view.currentAction0f : 0;
    const std::uint8_t pending = haveBlock ? view.pendingAction0e : 0;

    // Outside the casting band 0x85..0x8D the charge is stale, so it is dropped.
    if (static_cast<std::uint8_t>(action + 0x7Bu) > 8u)
    {
      marker.markerCharge19a = 0;
    }

    bool keepOpen = false;
    if (static_cast<std::int16_t>(marker.animationA0) != 2)
    {
      if (caster.typeId00 < 0 || pending == 0x0B || !haveBlock || action == 0x83 ||
          action == 0x85 || action == 0x87)
      {
        FUN_00225bc8_set_animation(marker, 2);
      }
      // The ring only exists while the character is drawn, standing on the
      // ground, and not flagged out by +0x96 bit 2. Anything else closes it.
      const bool casterDrawn = (caster.halfword08 & 1u) == 0;
      const bool casterGrounded = (caster.collisionFlags0c & 1u) != 0;
      if (casterDrawn && casterGrounded && (caster.effectFlags96 & 4u) == 0)
      {
        keepOpen = true;
      }
      if (!keepOpen)
      {
        marker.halfword08 = static_cast<std::uint16_t>(marker.halfword08 | 1u);
        FUN_00225bc8_set_animation(marker, 2);
      }
    }

    // The open. Only from the battle idle action, which is why the ring is
    // already up before a spell is ever cast.
    if (haveBlock && action == 6 && pending != 0x0B && (marker.halfword08 & 1u) != 0 &&
        (caster.collisionFlags0c & 1u) != 0)
    {
      FUN_00225bc8_set_animation(marker, 1);
      marker.scaleZ150 = 0.5f;
      marker.scale14c = 1.0f;
    }
    if (action == 0x83)
    {
      marker.halfword08 = static_cast<std::uint16_t>(marker.halfword08 | 1u);
      FUN_00225bc8_set_animation(marker, 2);
      marker.flags06 = static_cast<std::uint16_t>(marker.flags06 | 1u);
    }

    const std::int16_t step = static_cast<std::int16_t>(marker.animationA0);
    if (step == 1)
    {
      marker.markerCharge19a = 0;
      marker.markerFlags19e = static_cast<std::uint16_t>(marker.markerFlags19e | 1u);
      marker.flags06 = static_cast<std::uint16_t>(marker.flags06 & 0xFFEFu);
      marker.halfword08 = static_cast<std::uint16_t>(marker.halfword08 & 0xFFFEu);
      marker.markerRingTimer19c = static_cast<std::uint16_t>(FUN_00248e48_arm_timer(0x3C));
      if ((marker.flags06 & 1u) != 0)
      {
        FUN_00225bc8_set_animation(marker, 0);
      }
      return;
    }
    if (step == 0)
    {
      // The pulse. Gated on the charge, so a standing character's ring is
      // still while a charging one's throbs.
      if (marker.markerCharge19a == 0 || marker.markerRingTimer19c == 0)
      {
        return;
      }
      marker.markerRingTimer19c =
          FUN_00248e58_step_timer(marker.markerRingTimer19c, environment.frameTicks);
      if (marker.markerRingTimer19c != 0)
      {
        return;
      }
      if ((marker.markerFlags19e & 1u) == 0 &&
          (action == 0x86 || action == 0x8A || action == 0x8C) &&
          environment.DAT_0031d3c8_battleTableWord && environment.FUN_00267d38_playSound)
      {
        // FUN_00249308: the party record's four attack bytes for the selected
        // slot. Its first halfword is the element bit set, and the cue is
        // chosen off it -- 0xCB when none of them is set.
        const std::uint32_t element =
            environment.DAT_0031d3c8_battleTableWord(view.spellBlockAddress) & 0xFFFFu;
        std::uint16_t cue = 0xCB;
        if ((element & 0x004u) != 0) { cue = 0xDB; }
        else if ((element & 0x002u) != 0) { cue = 0xDC; }
        else if ((element & 0x020u) != 0) { cue = 0xDD; }
        else if ((element & 0x010u) != 0) { cue = 0xDE; }
        else if ((element & 0x400u) != 0) { cue = 0xDF; }
        else if ((element & 0x040u) != 0) { cue = 0xDB; }
        environment.FUN_00267d38_playSound(cue, marker);
      }
      marker.markerFlags19e = static_cast<std::uint16_t>(marker.markerFlags19e & 0xFFFEu);
      const std::uint16_t armed = static_cast<std::uint16_t>(FUN_00248e48_arm_timer(0x3C));
      marker.markerRingTimer19c = armed;
      if (marker.markerCharge19a > 299)
      {
        // Past two thirds of a full charge the pulses come at 5/8 the interval.
        marker.markerRingTimer19c = static_cast<std::uint16_t>((armed >> 1) + (armed >> 3));
      }
      if (environment.descriptors != nullptr)
      {
        const std::size_t pulse =
            pool.FUN_00265e28_allocate_and_initialize(399, *environment.descriptors);
        if (pulse < kEntitySlotCount)
        {
          auto &ring = pool.slot(pulse);
          ring.scaleZ150 = marker.scaleZ150;
          ring.scale14c = marker.scale14c;
          // Not the character: the pulse rides *this marker*, and reads its
          // +0x95 of 0 as "you are a pulse".
          ring.markerCaster198 = static_cast<std::uint16_t>(slot & 0xFFu);
          ring.depthBias133 = marker.depthBias133;
          ring.positionX20 = marker.positionX20;
          ring.positionZ24 = marker.positionZ24;
          ring.positionY28 = marker.positionY28;
          ring.groundHeight4c = marker.positionY28;
          marker.previousGroundHeight50 = marker.positionY28;
        }
      }
      return;
    }
    if (step == 2)
    {
      marker.markerRingTimer19c = static_cast<std::uint16_t>(FUN_00248e48_arm_timer(0x3C));
      marker.markerCharge19a = 0;
      marker.scaleZ150 *= 0.9f; // fGpffffa8b4
      marker.scale14c *= 0.9f;
      if ((marker.flags06 & 1u) != 0)
      {
        marker.flags06 = static_cast<std::uint16_t>(marker.flags06 | 0x10u);
        marker.scale14c = 1.0f;
        marker.halfword08 = static_cast<std::uint16_t>(marker.halfword08 | 1u);
        marker.scaleZ150 = 0.5f;
      }
      return;
    }
    if (step == 3 && (marker.flags06 & 1u) != 0)
    {
      FUN_00225bc8_set_animation(marker, 0);
    }
  }

  // ---------------------------------------------------------- Hand of Pyro
  //
  // Triangle's spell, and the first command whose *whole* chain is ported:
  //
  //   FUN_0024c058  state 111, the hold      -> charge accumulates
  //   FUN_0024bae0  state 109, the release   -> effect +0x60 = 1, +0x94 = level
  //   FUN_002da8a0  type 0x13D, the hand     -> FUN_002dab70 on +0x60 == 1
  //   FUN_002dab70                           -> spawns one type 0x15B
  //   FUN_002dae60  type 0x15B, the fireball -> flies, and spawns the next one
  //
  // **A cast produces exactly one fireball.** The rest of the volley is a
  // chain: each fireball arms +0x62 and spawns its successor when it expires,
  // one link per charge level. Nothing anywhere loops over a count.

  // FUN_002da220 (0x002da220). The point light under a spell effect's hand,
  // colour and radius passed by the caller, positioned on the caster's bone.
  // Allocated lazily into the effect's own +0x195 so it is released with it.
  void FUN_002da220_spell_light(OriginalEntity &effect,
                                std::size_t casterSlot,
                                std::int32_t chargeTimer3c,
                                std::uint8_t red,
                                std::uint8_t green,
                                std::uint8_t blue,
                                std::int16_t baseRadius,
                                const ActorEnvironment &environment)
  {
    if (environment.DAT_00343888_lights == nullptr)
    {
      return;
    }
    if (effect.lightSlot195 < 0)
    {
      // FUN_0023eb20: the high allocator first, then the low one. Slots 0..2 are
      // the directional lights an entity is lit by; 3.. are the flat tints, and
      // an effect wants one of those.
      const std::int32_t allocated =
          environment.DAT_00343888_lights->FUN_00266008_allocateFromThree() >= 0
              ? environment.DAT_00343888_lights->FUN_00266008_allocateFromThree()
              : environment.DAT_00343888_lights->FUN_00266050_allocateFromZero();
      if (allocated < 0)
      {
        return;
      }
      effect.lightSlot195 = static_cast<std::int8_t>(allocated);
      environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(allocated)).radius =
          0.2f; // DAT_0035482c
    }

    auto &light = environment.DAT_00343888_lights->slot(
        static_cast<std::uint32_t>(effect.lightSlot195));
    // FUN_00249270(caster, 3): the charge accumulator, capped at 0x2580 and
    // divided by a very fine divisor, so the glow swells continuously while the
    // spell is held rather than in the four visible charge steps.
    std::int32_t charge = chargeTimer3c > 0x2580 ? 0x2580 : chargeTimer3c;
    charge /= 3;
    light.red = red;
    light.green = green;
    light.blue = blue;
    light.radius = static_cast<float>(charge + baseRadius) / 1000.0f;
    if (environment.FUN_0020dc88_bone_point)
    {
      const auto point = environment.FUN_0020dc88_bone_point(
          casterSlot, static_cast<std::size_t>(effect.attachBone194),
          orphen::ported::psm2::Vec3{0.0f, 0.0f, 0.0f});
      light.x = point.x;
      light.y = point.y;
      light.z = point.z;
    }
  }

  // FUN_002dab70 (0x002dab70). Spawn one type 0x15B fireball at `origin`,
  // aimed at `target`, and arm the chain link that spawns the next one.
  //
  // `chainIndex` is the original's param_1: 0 for the one the hand throws, and
  // one higher for each successor. It selects the spread -- index 0 rises,
  // index 1 goes right, index 2 goes left, index 3+ dips -- so a volley fans
  // out without anything computing a fan.
  std::int32_t FUN_002dab70_spawn_fireball(std::uint8_t chainIndex,
                                           std::uint8_t chargeLevel,
                                           std::uint16_t attackPower,
                                           std::int16_t target,
                                           std::uint32_t hitParameters,
                                           float originX,
                                           float originZ,
                                           float originY,
                                           std::int16_t casterSlot,
                                           const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr || environment.descriptors == nullptr)
    {
      return -1;
    }
    EntityPool &pool = *environment.entityPool;
    const std::size_t spawned =
        pool.FUN_00265e28_allocate_and_initialize(0x15B, *environment.descriptors);
    if (spawned >= kEntitySlotCount)
    {
      return -1;
    }
    auto &ball = pool.slot(spawned);

    if (chainIndex == 0)
    {
      ball.effectFlags96 = static_cast<std::uint8_t>(ball.effectFlags96 | 0x40u);
    }
    ball.attackPower12c = attackPower;
    ball.fireballOriginX19c = originX;
    ball.positionX20 = originX;
    ball.fireballOriginZ1a0 = originZ;
    ball.positionZ24 = originZ;
    ball.fireballOriginY1a4 = originY;
    ball.positionY28 = originY;
    // FUN_00267da0(ball + 0x198, source, 4): the four attack bytes are copied,
    // not aliased. On the hand effect +0x198 is a pointer into the party
    // record; here it is the value.
    ball.hitParameters198 = hitParameters;
    ball.fireballTarget1c0 = target;
    ball.fireballCaster1c2 = casterSlot;
    ball.fireballCharge1c7 = chargeLevel;
    ball.fireballChain1c6 = chainIndex;
    ball.fadeRamp62 = 0;
    ball.animationA0 = 0;

    // Its own light, orange, fixed radius 2.0.
    if (environment.DAT_00343888_lights != nullptr)
    {
      if (ball.lightSlot195 < 0)
      {
        const std::int32_t high = environment.DAT_00343888_lights->FUN_00266008_allocateFromThree();
        const std::int32_t allocated =
            high >= 0 ? high : environment.DAT_00343888_lights->FUN_00266050_allocateFromZero();
        ball.lightSlot195 = static_cast<std::int8_t>(allocated);
      }
      if (ball.lightSlot195 >= 0)
      {
        auto &light =
            environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(ball.lightSlot195));
        light.radius = 2.0f;
        light.red = 0x80;
        light.green = 0x40;
        light.blue = 0x00;
        light.x = ball.positionX20;
        light.y = ball.positionZ24;
        light.z = ball.positionY28;
      }
    }

    if (chargeLevel != 0)
    {
      if (chargeLevel == chainIndex)
      {
        // The last link. No successor timer -- this is what ends the chain.
        if (chargeLevel == 5)
        {
          chargeLevel = 0x18;
          ball.animationA0 = 1;
        }
        else
        {
          ball.fireballSparkId19b = static_cast<std::uint8_t>(chargeLevel + 0x14);
        }
      }
      else
      {
        // The successor fires in `8 - level` frames, so a bigger charge throws
        // a faster stream as well as a longer one.
        ball.fadeRamp62 = FUN_00248e48_arm_timer(8u - chargeLevel);
      }
      if (chargeLevel == 5)
      {
        ball.fireballSparkId19b = 0x18;
      }
    }

    // Aimed, or not. Slots 0 and 1 are not targets -- `1 < target` is the
    // original's own test, and it is why the no-target case needs no special
    // casing here either.
    const bool aimed = target > 1 &&
                       static_cast<std::size_t>(target) < kEntitySlotCount &&
                       pool.slot(static_cast<std::size_t>(target)).typeId00 != 0;
    ball.facingRadians5c = pool.slot(static_cast<std::size_t>(casterSlot)).facingRadians5c;
    ball.fireballLife1c4 = FUN_00248e48_arm_timer(0x36);
    if (aimed)
    {
      const auto &victim = pool.slot(static_cast<std::size_t>(target));
      const float dx = victim.positionX20 - originX;
      const float dz = victim.positionZ24 - originZ;
      const float dy = victim.positionY28 - originY;
      // FUN_00216648: the 3D distance, divided by 1/144 -- i.e. the flight time
      // in ticks at the fixed speed. Both rates below are "distance over that".
      const float flight = std::sqrt(dx * dx + dz * dz + dy * dy) / 0.00694444f;
      if (flight > 0.0f)
      {
        ball.fireballRise1b0 =
            ((victim.positionY28 + victim.height58 * 0.5f) - originY) / flight;
        ball.fireballSpeed1bc = std::sqrt(dx * dx + dz * dz) / flight;
      }
    }
    else
    {
      ball.fireballSpeed1bc = 0.00694444f; // uGpffffa8f4
      ball.fireballRise1b0 = 0.0f;
    }
    ball.fireballBaseFacing1b4 = ball.facingRadians5c;

    if (environment.FUN_00267d38_playSound)
    {
      environment.FUN_00267d38_playSound(0xCC, ball);
    }
    return static_cast<std::int32_t>(spawned);
  }

  // FUN_002dae60 (0x002dae60), the behaviour of type 0x15B -- one fireball.
  //
  // Everything about the volley lives here: the spread that makes a chain fan
  // out, and the +0x62 timer that spawns the next link.
  void FUN_002dae60_fireball(OriginalEntity &ball,
                             std::size_t slot,
                             const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }

    // FUN_002660d0: republish the light at the new position.
    if (ball.lightSlot195 >= 0 && environment.DAT_00343888_lights != nullptr)
    {
      auto &light =
          environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(ball.lightSlot195));
      light.x = ball.positionX20;
      light.y = ball.positionZ24;
      light.z = ball.positionY28;
    }
    ball.hitSourceC0 = 0;
    ball.hitFlagsC2 = 0;
    ball.pendingDamageBe = 0;
    ball.hitReactionBc = 0;
    ball.halfword08 = static_cast<std::uint16_t>(ball.halfword08 | 0x4000u);

    // FUN_00215e48 then FUN_002148a8: clear the already-hit set, then sweep.
    std::int8_t contacts = 0;
    if (environment.hitTest != nullptr)
    {
      FUN_00215e48_clear_hit_set(ball);
      const auto parameters =
          orphen::ported::resource::HitParameters::unpack(ball.hitParameters198);
      contacts = FUN_002148a8_swept_hit_test(ball, slot, parameters, *environment.hitTest);
    }
    if (contacts != 0)
    {
      // It hit something: burst into a type 0x173 and go. The burst inherits
      // this one's light so the flash does not blink out on the swap.
      if (environment.descriptors != nullptr)
      {
        const std::size_t burst = environment.entityPool->FUN_00265e28_allocate_and_initialize(
            0x173, *environment.descriptors);
        if (burst < kEntitySlotCount)
        {
          auto &flash = environment.entityPool->slot(burst);
          flash.positionX20 = ball.positionX20;
          flash.positionZ24 = ball.positionZ24;
          flash.positionY28 = ball.positionY28;
          flash.groundHeight4c = ball.positionY28;
          flash.previousGroundHeight50 = ball.positionY28;
          flash.facingRadians5c = ball.facingRadians5c + 3.14159274f; // fGpffffa8f8
          flash.rotationX154 = -ball.rotationX154;
          if (ball.lightSlot195 >= 0)
          {
            flash.lightSlot195 = ball.lightSlot195;
            ball.lightSlot195 = -1;
          }
          FUN_00225bc8_set_animation(flash, 0);
        }
      }
      FUN_00265ec0_destroy_entity(slot, environment);
      return;
    }

    // +0x1C4 is the life timer, and 0x6C0 is its armed value: on the first
    // frame the facing is banked into +0x1B4 and zeroed, because the spread
    // below is measured from it rather than added to it.
    if (ball.fireballLife1c4 == 0x6C0)
    {
      ball.fireballBaseFacing1b4 = ball.facingRadians5c;
      ball.facingRadians5c = 0.0f;
    }
    ball.fireballLife1c4 = FUN_00248e58_step_timer(ball.fireballLife1c4, environment.frameTicks);
    if (ball.fireballLife1c4 == 0)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
      return;
    }

    // The spread, by position in the chain. Only two of the four cases turn;
    // the others climb or dip, which is what gives a volley its arc.
    std::int32_t rise = 0;
    std::int32_t turn = 0;
    const std::uint8_t chain = ball.fireballChain1c6;
    if (chain == 0)
    {
      rise = 6;
      if ((ball.fireballCharge1c7 ^ 5u) != 0)
      {
        rise = 0;
      }
    }
    else if (chain == 1)
    {
      if (ball.fireballCharge1c7 == 1)
      {
        rise = -8;
      }
      else
      {
        turn = 0x32;
      }
    }
    else if (chain == 2)
    {
      turn = -0x32;
    }
    else
    {
      rise = -8;
      if ((chain ^ 3u) != 0)
      {
        rise = 0;
      }
    }

    // How far through its life it is, in the original's own units: 0x360 minus
    // the remaining ticks. Both the turn and the climb ramp with it.
    const float elapsed = static_cast<float>(
        static_cast<std::int16_t>(0x360 - static_cast<std::int32_t>(ball.fireballLife1c4)));
    constexpr float kTwoPi = 6.28318548f; // fGpffffa8fc
    ball.facingRadians5c =
        ((static_cast<float>(turn) * kTwoPi) / 360.0f / 1728.0f) * elapsed +
        ball.fireballBaseFacing1b4;
    ball.fireballVelX1a8 = ball.fireballSpeed1bc * std::sin(ball.facingRadians5c);
    ball.fireballVelZ1ac = ball.fireballSpeed1bc * std::cos(ball.facingRadians5c);
    const float planar =
        std::sqrt(ball.fireballVelX1a8 * ball.fireballVelX1a8 +
                  ball.fireballVelZ1ac * ball.fireballVelZ1ac);
    ball.fireballRiseOffset1b8 =
        ((static_cast<float>(rise) * kTwoPi) / 360.0f / 1728.0f) * elapsed;
    ball.desiredDeltaY38 = ball.fireballRise1b0 * static_cast<float>(environment.frameTicks) +
                   ball.fireballRiseOffset1b8;
    ball.rotationX154 = std::atan2(-ball.fireballRise1b0, planar);
    ball.desiredDeltaX30 = ball.fireballVelX1a8 * static_cast<float>(environment.frameTicks);
    ball.desiredDeltaZ34 = ball.fireballVelZ1ac * static_cast<float>(environment.frameTicks);

    // The chain. +0x62 was armed by FUN_002dab70 only when this is not the last
    // link, so an expiring timer here always means "there is another one".
    if (ball.fadeRamp62 != 0)
    {
      ball.fadeRamp62 = FUN_00248e58_step_timer(ball.fadeRamp62, environment.frameTicks);
      if (ball.fadeRamp62 == 0)
      {
        FUN_002dab70_spawn_fireball(static_cast<std::uint8_t>(ball.fireballChain1c6 + 1),
                                    ball.fireballCharge1c7, ball.attackPower12c,
                                    ball.fireballTarget1c0, ball.hitParameters198,
                                    ball.fireballOriginX19c, ball.fireballOriginZ1a0,
                                    ball.fireballOriginY1a4, ball.fireballCaster1c2, environment);
      }
    }

    // +0x0C bits 0x4066: hit a wall, hit the floor, or left the map.
    if ((ball.collisionFlags0c & 0x4066u) != 0)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
    }
  }

  // FUN_002da8a0 (0x002da8a0), the behaviour of type 0x13D -- the flame that
  // gathers in Orphen's hand while Triangle is held, and throws itself when it
  // is released.
  //
  // The throw is one line of it: `+0x60 == 1`, which FUN_0024bae0 writes on the
  // frame the release animation's +0xAA bit 0x100 marker comes round. Everything
  // before that is presentation.
  void FUN_002da8a0_hand_effect(OriginalEntity &effect,
                                std::size_t slot,
                                const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    const std::int16_t casterIndex = effect.parentSlot192;
    if (casterIndex < 0 || static_cast<std::size_t>(casterIndex) >= kEntitySlotCount)
    {
      return;
    }
    const std::size_t casterSlot = static_cast<std::size_t>(casterIndex);
    OriginalEntity &caster = pool.slot(casterSlot);

    const std::uint16_t entryFlags08 = effect.halfword08;
    effect.depthBias133 = -0x0C;
    effect.facingRadians5c = 1.57079637f; // uGpffffa8e0
    effect.halfword08 = static_cast<std::uint16_t>(entryFlags08 | 0x4000u);

    ActorEnvironment::BattleMemberView view;
    const std::uint32_t member = static_cast<std::uint32_t>(caster.byte95) - 1u;
    const bool haveBlock = caster.byte95 != 0 && environment.DAT_0031d7b0_battleMember &&
                           environment.DAT_0031d7b0_battleMember(member, view);

    std::int16_t step = static_cast<std::int16_t>(effect.animationA0);
    if (step != 2)
    {
      if (haveBlock && view.pendingAction0e == 0x0B)
      {
        effect.halfword08 = static_cast<std::uint16_t>(entryFlags08 | 0x4001u);
        FUN_00225bc8_set_animation(effect, 2);
      }
      // `1 < (byte)(current + 0x76)` -- true for anything outside the two action
      // bytes 0x8A and 0x8B, i.e. "the caster is no longer casting this spell".
      const std::uint8_t action = haveBlock ? view.currentAction0f : 0;
      if (static_cast<std::uint8_t>(action + 0x76u) > 1u)
      {
        effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 | 1u);
        FUN_00225bc8_set_animation(effect, 2);
      }
      step = static_cast<std::int16_t>(effect.animationA0);
    }

    if (step == 1)
    {
      // The gather. Its tilt is the character class's, and this is where it
      // becomes visible.
      if (haveBlock && view.characterClass == 1)
      {
        effect.rotationX154 = 0.698131740f; // uGpffffa8e4
      }
      else if (haveBlock && view.characterClass == 4)
      {
        effect.rotationX154 = 1.83259571f;  // uGpffffa8e8
        effect.rotationY158 = -0.174532920f; // uGpffffa8ec
      }
      const std::uint16_t flags06 = effect.flags06;
      effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 & 0xFFFEu);
      effect.flags06 = static_cast<std::uint16_t>(flags06 & 0xFFEFu);
      if ((flags06 & 1u) != 0)
      {
        FUN_00225bc8_set_animation(effect, 0);
      }
    }
    else if (step == 0)
    {
      // The hold. The hand light swells with the charge.
      FUN_002da220_spell_light(effect, casterSlot, haveBlock ? view.chargeTimer3c : 0, 0x80, 0x40,
                               0x00, 500, environment);
    }
    else if (step == 2)
    {
      // Put away. FUN_00266098 drops the light, and the scale is reset so the
      // next cast starts from 1.
      if (effect.lightSlot195 >= 0 && environment.DAT_00343888_lights != nullptr)
      {
        environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(effect.lightSlot195))
            .radius = 0.0f;
        effect.lightSlot195 = -1;
      }
      if ((effect.flags06 & 1u) != 0)
      {
        effect.flags06 = static_cast<std::uint16_t>(effect.flags06 | 0x10u);
        effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 | 1u);
        effect.scale14c = 1.0f;
        effect.scaleZ150 = 1.0f;
      }
    }

    // **The throw.** State 60 is 1 for exactly one frame, written by
    // FUN_0024bae0 when the release animation reaches its marker.
    if (effect.state60 != 1)
    {
      return;
    }
    effect.flags06 = static_cast<std::uint16_t>(effect.flags06 & 0xFFEFu);
    effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 & 0xFFFEu);
    FUN_00225bc8_set_animation(effect, 2);
    effect.state60 = 0;

    // The fireball leaves the caster's hand bone, not the effect's position.
    orphen::ported::psm2::Vec3 origin{effect.positionX20, effect.positionZ24, effect.positionY28};
    if (environment.FUN_0020dc88_bone_point)
    {
      origin = environment.FUN_0020dc88_bone_point(
          casterSlot, static_cast<std::size_t>(effect.attachBone194),
          orphen::ported::psm2::Vec3{0.0f, 0.0f, 0.0f});
    }
    // FUN_00267da0 copies the four attack bytes off the party record the
    // caster's +0x198 points at; on the projectile they are a value.
    const std::uint32_t hitParameters =
        environment.DAT_0031d3c8_battleTableWord
            ? environment.DAT_0031d3c8_battleTableWord(effect.hitParameters198)
            : 0;
    FUN_002dab70_spawn_fireball(0, effect.spawnParam94, effect.attackPower12c,
                                haveBlock ? view.target : -1, hitParameters, origin.x, origin.y,
                                origin.z, static_cast<std::int16_t>(casterSlot), environment);
  }

  // FUN_002da350 (0x002da350), the behaviour of type 0x139 -- the sword the
  // battle module puts in Orphen's hand for the kind-0 arm, and of type 0x13E,
  // the same effect in red.
  //
  // **Its first four lines are why the swing looks right.** The blade forces
  // itself back to animation 2 whenever the caster is not in action 0x84
  // (swinging) or 0x86 (charging). State 106, the approach, is action *0x85* --
  // neither -- so the blade lands on animation 2 on the frame 106 runs, and
  // FUN_0024ac88's entry gate (`if (blade->anim != 2) { if (!(+0x06 & 0x10))
  // ...re-press... }`) therefore takes the **setup** branch rather than the
  // chain-advance one.
  //
  // That matters because FUN_00248e98 and FUN_00248ee0 both end in
  // `+0x06 &= 0xFFEE`, which clears bit 0x10 -- so state 106, which sets the run
  // animation on entry, always hands 105 a character with that bit down. Without
  // this handler the blade stayed on animation 1, 105 read it as a re-press, and
  // the character held the *run* animation until it happened to raise the swing
  // marker: a visible run-up before every swing, and a first slash that never
  // ran its setup.
  //
  // The rest is the blade's own life: a point light on its bone 1, the swept hit
  // test while the caster swings, and a 1.5x damage bump on the third slash.
  void FUN_002da350_battle_blade(OriginalEntity &blade,
                                 std::size_t slot,
                                 const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    // +0x192 is read as a *byte* here, not the signed halfword the attachment
    // uses, so a detached blade reads caster 0xFF rather than -1.
    const std::uint8_t casterIndex = static_cast<std::uint8_t>(blade.parentSlot192 & 0xFF);
    if (casterIndex >= kEntitySlotCount)
    {
      return;
    }
    OriginalEntity &caster = pool.slot(casterIndex);

    // The blade rides a bone, so its own transform is the identity the
    // attachment is applied to. uGpffff88c0 = pi/2.
    blade.positionX20 = 0.0f;
    blade.positionZ24 = 0.0f;
    blade.positionY28 = 0.0f;
    blade.facingRadians5c = 1.57079637f;

    ActorEnvironment::BattleMemberView view;
    const std::uint32_t member = static_cast<std::uint32_t>(caster.byte95) - 1u;
    const bool haveBlock = caster.byte95 != 0 && environment.DAT_0031d7b0_battleMember &&
                           environment.DAT_0031d7b0_battleMember(member, view);
    const std::uint8_t action = haveBlock ? view.currentAction0f : 0;

    // ---- the four lines above ----
    if (static_cast<std::int16_t>(blade.animationA0) != 2)
    {
      if (haveBlock && view.pendingAction0e == 0x0B)
      {
        FUN_00225bc8_set_animation(blade, 2);
      }
      if (action != 0x84 && action != 0x86)
      {
        FUN_00225bc8_set_animation(blade, 2);
      }
    }

    // +0x62 is the re-hit cooldown, five frames between contacts.
    if (blade.fadeRamp62 != 0)
    {
      // FUN_00248e58, written out rather than reached for: the entity layer does
      // not depend on the battle module, and this is three lines.
      const std::uint16_t stepped =
          static_cast<std::uint16_t>(blade.fadeRamp62 - environment.frameTicks);
      blade.fadeRamp62 = (blade.fadeRamp62 < stepped) ? std::uint16_t{0} : stepped;
    }

    const std::int16_t step = static_cast<std::int16_t>(blade.animationA0);
    if (step == 1)
    {
      // Drawing it. The light is allocated once, on the frame the blade appears.
      if (blade.lightSlot195 < 0 && environment.DAT_00343888_lights != nullptr)
      {
        // FUN_0023eb20: the high allocator first, then the low one.
        std::int32_t allocated = environment.DAT_00343888_lights->FUN_00266008_allocateFromThree();
        if (allocated < 0)
        {
          allocated = environment.DAT_00343888_lights->FUN_00266050_allocateFromZero();
        }
        if (allocated >= 0)
        {
          blade.lightSlot195 = static_cast<std::int8_t>(allocated);
          auto &light = environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(allocated));
          // 0x139 is the blue blade, everything else the red one.
          if (blade.typeId00 == 0x139)
          {
            light.red = 0x96;
            light.green = 0x96;
            light.blue = 0xE6;
          }
          else
          {
            light.red = 0xE6;
            light.green = 0x96;
            light.blue = 0x96;
          }
          light.radius = 0.2f; // DAT_00354834
          environment.DAT_00343888_lights->noteRadius(static_cast<std::uint32_t>(allocated), 0.2f);
        }
      }
      if ((blade.halfword08 & 1u) != 0 && environment.FUN_00267d38_playSound)
      {
        environment.FUN_00267d38_playSound(0xCD, blade); // the blade appearing
      }
      const std::uint16_t entryFlags06 = blade.flags06;
      blade.halfword08 = static_cast<std::uint16_t>(blade.halfword08 & 0xFFFEu);
      blade.flags06 = static_cast<std::uint16_t>(entryFlags06 & 0xFFEFu);
      if ((entryFlags06 & 1u) != 0)
      {
        // The draw-on animation finished: animation 0 is the one that can hit.
        FUN_00225bc8_set_animation(blade, 0);
      }
    }
    else if (step == 2)
    {
      // Put away. The light fades at a thousandth of a tick and the slot goes
      // back when it drops under 0.2.
      if (blade.lightSlot195 >= 0 && environment.DAT_00343888_lights != nullptr)
      {
        auto &light = environment.DAT_00343888_lights->slot(
            static_cast<std::uint32_t>(blade.lightSlot195));
        const float faded = light.radius - static_cast<float>(environment.frameTicks) / 1000.0f;
        const bool spent = faded < 0.2f; // DAT_0035483c
        light.radius = faded;
        if (spent)
        {
          light.radius = 0.0f; // FUN_00266098
          blade.lightSlot195 = -1;
        }
      }
      if ((blade.flags06 & 1u) != 0)
      {
        blade.flags06 = static_cast<std::uint16_t>(blade.flags06 | 0x10u);
        blade.scale14c = 1.0f;
        blade.halfword08 = static_cast<std::uint16_t>(blade.halfword08 | 1u);
        blade.scaleZ150 = 1.0f;
      }
      return;
    }
    else if (step != 0)
    {
      return;
    }

    // ---- the hit test, on animations 0 and 1 ----
    //
    // The blade borrows the caster's facing for the duration of the sweep and
    // puts it back to zero afterwards, because +0x5C is also what the bone
    // attachment composes with.
    blade.facingRadians5c = caster.facingRadians5c;
    std::int8_t contacts = 0;
    if (action == 0x84 && environment.hitTest != nullptr)
    {
      // FUN_00267da0 copies the four attack bytes out of the party record the
      // blade's +0x198 points at -- an address, not a value, on this path.
      const std::uint32_t packed = environment.DAT_0031d3c8_battleTableWord
                                       ? environment.DAT_0031d3c8_battleTableWord(blade.hitParameters198)
                                       : 0;
      auto parameters = orphen::ported::resource::HitParameters::unpack(packed);
      if (caster.animationA0 == 0x31)
      {
        // The third slash: reaction 0x1B, and half again the power bonus with
        // the low bit forced, so it is always a real increase.
        parameters.reaction = 0x1B;
        const std::int8_t bonus = parameters.powerBonus;
        parameters.powerBonus =
            static_cast<std::int8_t>(bonus + ((static_cast<int>(bonus) / 2) | 1));
      }
      // +0x96 bit 0x40 is the player's own instant-kill path in the hit test,
      // and only the lead gets it.
      if (casterIndex == 0)
      {
        blade.effectFlags96 = static_cast<std::uint8_t>(blade.effectFlags96 | 0x40u);
      }
      contacts = FUN_002148a8_swept_hit_test(blade, slot, parameters, *environment.hitTest);
      blade.effectFlags96 = static_cast<std::uint8_t>(blade.effectFlags96 & 0xBFu);
    }
    blade.facingRadians5c = 0.0f;

    if (contacts != 0 && blade.fadeRamp62 == 0)
    {
      if (environment.FUN_00267d38_playSound)
      {
        environment.FUN_00267d38_playSound(0xCE, blade); // the hit
      }
      // FUN_00248e48(5): `(n << 0x15) >> 0x10`, five frames of ticks.
      blade.fadeRamp62 = static_cast<std::uint16_t>(5 * 32);
    }

    if (blade.lightSlot195 >= 0 && environment.DAT_00343888_lights != nullptr)
    {
      auto &light =
          environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(blade.lightSlot195));
      light.radius = 1.3f; // DAT_00354838
      environment.DAT_00343888_lights->noteRadius(static_cast<std::uint32_t>(blade.lightSlot195),
                                                  1.3f);
      if (environment.FUN_0020dc88_bone_point)
      {
        // FUN_0020dc88(blade, 1, offset, lightSlot): bone 1 of the *blade*, not
        // the caster, plus DAT_0034fad8's (0, 0, 0.8) -- so the light sits up
        // the length of the blade rather than at the hilt.
        const auto point = environment.FUN_0020dc88_bone_point(
            slot, 1u, orphen::ported::psm2::Vec3{0.0f, 0.0f, 0.8f});
        light.x = point.x;
        light.y = point.y;
        light.z = point.z;
      }
    }
  }

  bool actorHandlerIsImplemented(std::uint32_t handlerAddress)
  {
    switch (handlerAddress)
    {
    case kFUN_00239e78_noOp:
    case 0x00258AB8u: // FUN_00258ab8, type 0x37, the party follower
    case 0x002D1EA8u: // FUN_002d1ea8, type 0x3A
    case 0x0025AB68u: // FUN_0025ab68, party members
    case 0x002CD0A0u: // FUN_002cd0a0, the type 0x62 enemy
    case 0x00213720u: // FUN_00213720, type 0x19, the player's bandana
    case 0x0025BF20u: // FUN_0025bf20, type 0x38, the script-driven NPC
    case 0x002D2F40u: // FUN_002d2f40, type 0x28, the close-up rig
    case 0x002D21B8u: // FUN_002d21b8, type 0x42, the sword blade
    case 0x002D2470u: // FUN_002d2470, type 0x44, the homing magic projectile
    case 0x002F13D0u: // FUN_002f13d0, type 0x1E3, the shared hit effect
    case 0x002E7328u: // FUN_002e7328, type 0x1C7, the guard shield
    case 0x002DA350u: // FUN_002da350, type 0x139, the battle sword blade
    case 0x002DA8A0u: // FUN_002da8a0, type 0x13D, Hand of Pyro's hand effect
    case 0x002DAE60u: // FUN_002dae60, type 0x15B, the fireball it throws
    case 0x002D9C88u: // FUN_002d9c88, type 0x18F, the ground ring
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
    case 0x00258AB8u:
      return "FUN_00258ab8 (party follower)";
    case 0x002D1EA8u:
      return "FUN_002d1ea8 (treasure chest)";
    case 0x0025AB68u:
      return "FUN_0025ab68 (party member)";
    case 0x002CD0A0u:
      return "FUN_002cd0a0 (enemy)";
    case 0x00213720u:
      return "FUN_00213720 (player bandana)";
    case 0x0025BF20u:
      return "FUN_0025bf20 (script-driven NPC)";
    case 0x002D2F40u:
      return "FUN_002d2f40 (close-up rig)";
    case 0x002D21B8u:
      return "FUN_002d21b8 (sword blade)";
    case 0x002D2470u:
      return "FUN_002d2470 (magic projectile)";
    case 0x002F13D0u:
      return "FUN_002f13d0 (shared hit effect)";
    case 0x002E7328u:
      return "FUN_002e7328 (guard shield)";
    case 0x002DA350u:
      return "FUN_002da350 (battle sword blade)";
    case 0x002DA8A0u:
      return "FUN_002da8a0 (hand of pyro)";
    case 0x002DAE60u:
      return "FUN_002dae60 (fireball)";
    case 0x002D9C88u:
      return "FUN_002d9c88 (cast marker)";
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

      // +0x30/+0x34/+0x38 are deliberately **not** cleared here.
      //
      // FUN_00239ce0 does not touch them: the physics pass owns the whole
      // accumulate-then-spend cycle, and FUN_002262c0 zeroes them once it has
      // applied them. `integrateNonPlayerMovement` does the same at its end, so
      // clearing here as well was a second, invented reset -- and it landed in
      // the worst possible place. The scene script's own tick runs *before* this
      // loop, so a scripted walk (`0xEE`..`0xF1`, which accumulate into +0x30
      // and +0x34 rather than writing position) had its request wiped on the
      // same frame it was made. Every script-driven actor stood still.
      switch (handler.address)
      {
      case 0x002D1EA8u:
        FUN_002d1ea8_treasure_chest(entity, slotEnvironment);
        break;
      case 0x00258AB8u:
        FUN_00258ab8_party_follower(entity, slotEnvironment, trace);
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
      case 0x0025BF20u:
        // FUN_0025bf20: install this entity as the selection and the focus, run
        // its own freeze gate, and on a clear frame run the body at +0x130.
        //
        // The original sets both globals *before* the gate, so a frozen NPC
        // still leaves itself in focus. The port sets the focus inside the
        // callback and so skips that on a frozen frame; nothing observes it,
        // because every body re-establishes the focus on entry.
        if (!FUN_0023a068_freeze_gate(entity, environment.frameTicks) &&
            environment.FUN_0025bf20_run_npc_body)
        {
          environment.FUN_0025bf20_run_npc_body(slot, entity.recordId130);
        }
        break;
      case 0x002D2F40u:
        FUN_002d2f40_build_closeup_rig(entity, slot, environment);
        break;
      case 0x002D21B8u:
        FUN_002d21b8_sword_effect(entity, slot, slotEnvironment);
        break;
      case 0x002D2470u:
        FUN_002d2470_magic_projectile(entity, slot, slotEnvironment);
        break;
      case 0x002F13D0u:
        FUN_002f13d0_shared_hit_effect(entity, slotEnvironment);
        break;
      case 0x002E7328u:
        FUN_002e7328_guard_shield(entity, slot, slotEnvironment);
        break;
      case 0x002DA350u:
        FUN_002da350_battle_blade(entity, slot, slotEnvironment);
        break;
      case 0x002DA8A0u:
        FUN_002da8a0_hand_effect(entity, slot, slotEnvironment);
        break;
      case 0x002DAE60u:
        FUN_002dae60_fireball(entity, slot, slotEnvironment);
        break;
      case 0x002D9C88u:
        FUN_002d9c88_cast_marker(entity, slot, slotEnvironment);
        break;
      case kFUN_00239e78_noOp:
      default:
        break;
      }

    }
  }

  // FUN_002261e0. **A second, separate walk of the pool.**
  //
  // The original's frame function (FUN_002239c8:116-135) is:
  //
  //     FUN_0025b778   script tick -- 0x55 placements, 0x7E group moves
  //     FUN_00251ed8   lead player
  //     FUN_00239ce0   actor behaviours
  //     FUN_00208450   collision groups, which sets DAT_003555d0
  //     FUN_002261e0   physics, which reads it
  //
  // Two things fall out of that and the port had neither. Every behaviour runs
  // before any physics -- not behaviour-then-physics per entity -- and, more
  // sharply, **FUN_00208450 and FUN_002261e0 are adjacent**, so a collision
  // group dirtied by this frame's script is seen by this frame's physics. The
  // port used to fuse physics into the behaviour loop and run the groups after
  // it, which left DAT_003555d0 a frame stale. One frame is the entire window:
  // a cutscene 0x55 that drops an actor inside scenery gets exactly one physics
  // pass to push it out before the vertical settle stands it on top, and a
  // stale flag misses it every time. That is why Magnus stood on the crates.
  //
  // The gates are FUN_002261e0:18-19: a positive status byte, +0x02 bit 0x800
  // clear, and no parent -- an attached entity's +0x20..+0x28 is an offset in
  // its parent bone's space, so integrating a movement request into it would
  // drag the attachment point off the bone.
  void FUN_002261e0_update_physics(const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    for (std::size_t slot = kFirstTickedSlot; slot < kEntitySlotCount; ++slot)
    {
      if (pool.status(slot) != SlotStatus::ScriptSpawned)
      {
        continue;
      }
      OriginalEntity &entity = pool.slot(slot);
      if ((entity.descriptorFlags02 & kHidden02) != 0)
      {
        continue;
      }
      if (entity.parentSlot192 >= 0)
      {
        continue;
      }
      integrateNonPlayerMovement(entity, environment, slot);
    }
  }

} // namespace orphen::ported::entity
