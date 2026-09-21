#include "ported/entity/actor_frame_update.h"

#include "ported/entity/original_battle_enemy.h"
#include "ported/entity/original_crab_boss.h"
#include "ported/entity/original_mast_boss.h"
#include "ported/entity/original_swarm_crab.h"
#include "ported/entity/original_bubble_effect.h"
#include "ported/entity/original_water_splash.h"
#include "ported/entity/original_enemy_attack.h"
#include "ported/entity/original_ship_fire.h"
#include "ported/entity/original_status_aura.h"
#include "ported/entity/original_summon_stage.h"
#include "ported/entity/original_field_hp_gauge.h"
#include "ported/entity/original_health_bar.h"

#include "ported/battle/battle_tables.h"
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
#include <span>
#include <cmath>
#include <vector>

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

  bool &DAT_003555d1_suspendPushOut()
  {
    static bool value = false;
    return value;
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

  std::int16_t FUN_0023a678_countdown(std::int16_t timer, std::uint32_t frameTicks)
  {
    const std::int16_t remaining =
        static_cast<std::int16_t>(timer - static_cast<std::int16_t>(frameTicks));
    return remaining < 1 ? static_cast<std::int16_t>(0) : remaining;
  }

  float FUN_0023a990_bezier(float t, const std::array<float, 3> &points)
  {
    const float inverse = 1.0f - t;
    return inverse * inverse * points[0] + (inverse + inverse) * t * points[1] +
           t * t * points[2];
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
    // :111. DAT_003555D1 is the outer gate on the whole push-out, ahead of the
    // collision-group test.
    if (!DAT_003555d1_suspendPushOut() && environment.FUN_00227390_corner_sample &&
        environment.DAT_003555d0_collisionGroupMoved && (entity.halfword08 & 0x0020u) != 0)
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

      // FUN_002262c0:0x00226884-0x00226cb4, the whole accept/refuse decision
      // for one destination. It is **FUN_00227390's return value** first, and
      // only then the 0.26 step:
      //
      //   lVar7 = FUN_00227390(dest);
      //   if (+0x4C - w[6] <= +0x7C) {                     // (A)
      //     if (+0x7C < w[5] - w[6]) refuse;               // (B)
      //     if (lVar7 != 0) accept;                        // <-- the real gate
      //     if ((+0x0C & 0x10000) == 0) {
      //       +0x0C |= 2;
      //       if (+0x28 != +0x50) refuse;                  // not settled
      //       if (w[5] - +0x28 < DAT_00352434) {           // 0.26, strict
      //         if (w[2] <= +0x80) { ...provisional raise, re-query... }
      //       }
      //     }
      //   } else refuse;
      //
      // `lVar7` is not "the scan answered". FUN_00227390 ends with
      //
      //   uVar1 = 0;
      //   if (fVar6 <= *(float *)(entity + 0x28)) { uVar1 = 1; ...required mask... }
      //
      // -- **1 only when the surface it found is at or below the feet.** So a
      // move onto anything higher than where the actor is standing never takes
      // the accept path at all; it falls through to the 0.26 branch, and that
      // branch is the only way up. This is the same rule the lead already
      // carries as `canStepToHeight`, and the non-player path had lost it.
      //
      // Gates (A) and (B) are the entity's own +0x7C, which is **100.0 on every
      // entity in the s14_e001 dump** -- crab, lead, props alike -- so neither
      // can fire and neither is modelled here; w[6], the lowest of the four
      // corners, is the only thing they need and TerrainSurface does not carry
      // it. (A) and (B) are noted rather than guessed at.
      //
      // What the missing gate cost: `s14_e001`'s arena is a pool floor at -1.0
      // inside a deck at -0.5, and the crab fights **in the pool** -- the save
      // state taken as the battle starts has it at (0.00, -3.18, -1.00) on
      // primitive 649, all four corners -1.000. Uncapped, its walk-in stepped
      // the half unit straight up onto the deck and it spent the fight
      // bobbing on and off the planking beside the player.
      //
      // Not modelled: the provisional raise the original makes inside the 0.26
      // branch (`+0x28 = w[5] + DAT_00352438`, re-query, accept only if the new
      // answer is still under 0.26), and DAT_00352434's use as the fallback
      // ledge probe when the corner scan finds nothing.
      constexpr float kDAT_00352434_stepHeight = 0.26f;
      const auto walkable = [&entity](const std::optional<ActorEnvironment::TerrainSurface> &at) {
        if (!at.has_value())
        {
          return false;
        }
        // FUN_00227390's `fVar6 <= entity +0x28`.
        if (at->height <= entity.positionY28)
        {
          return true;
        }
        // The step up. `+0x28 != +0x50` is an actor that is not settled on the
        // ground -- mid-fall, mid-hop -- and it may not step at all.
        if (entity.positionY28 != entity.previousGroundHeight50)
        {
          return false;
        }
        if (!(at->height - entity.positionY28 < kDAT_00352434_stepHeight))
        {
          return false;
        }
        return at->slopeAngle <= entity.slopeLimit80;
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
  //
  // **The cascade is the whole subtree, not the first layer of it.**
  // FUN_00265F70 does not release a child itself -- it calls FUN_00265EC0 on
  // each one, which runs FUN_00265F70 again on its way out. A one-level sweep
  // leaves a grandchild alive with its `+0x192` naming a slot that has already
  // been handed to something else. s14_e031 is where that showed: the scene
  // tears its first close-up rig down with three opcode 0x5C calls -- the
  // `0x26` bust, the `0x27` hair, the `0x28` mount -- and never names the
  // `0x19` cloth, because on hardware the bust takes its cloth with it. The
  // port kept the cloth, the slot it pointed at was recycled into the *next*
  // rig's cloth, and Orphen wore two bandanas through the whole close-up.
  //
  // The status byte is cleared before the rescan, which is also what stops a
  // parent cycle looping: the original zeroes DAT_005A96B0 at the top of
  // FUN_00265EC0, before FUN_00265F70 ever runs.
  void FUN_00265ec0_destroy_entity(std::size_t slot,
                                   EntityPool &pool,
                                   orphen::ported::render::LightTable *lights)
  {
    if (slot >= pool.slotCount())
    {
      return;
    }

    std::vector<std::size_t> pending{slot};
    while (!pending.empty())
    {
      const std::size_t current = pending.back();
      pending.pop_back();

      // `*entity < 1`: FUN_00265EC0's short branch clears +0x96, the type and
      // +0x95 and stops -- no light given back, and no cascade. The pool's
      // release is a superset of those three writes.
      if (pool.slot(current).typeId00 < 1)
      {
        pool.releaseSlot(current);
        continue;
      }

      // FUN_00266098.
      const std::int8_t lightSlot = pool.slot(current).lightSlot195;
      if (lightSlot >= 0 && lights != nullptr)
      {
        lights->slot(static_cast<std::uint32_t>(lightSlot)).radius = 0.0f;
      }
      pool.slot(current).lightSlot195 = -1;

      // Released before the scan below, standing in for the original's leading
      // status write. Nothing after this point reads the slot: FUN_0020E7E0's
      // sound handles are not modelled, and the `+0x02 & 0x8000` script hook is
      // the one piece of FUN_00265EC0 the port still does not have.
      pool.releaseSlot(current);

      // FUN_00265f70: every live slot whose +0x192 names this one, each of them
      // through FUN_00265EC0 again.
      for (std::size_t child = 0; child < pool.slotCount(); ++child)
      {
        if (child == current || pool.status(child) != SlotStatus::ScriptSpawned)
        {
          continue;
        }
        if (pool.slot(child).parentSlot192 == static_cast<std::int16_t>(current))
        {
          pending.push_back(child);
        }
      }
    }
  }

  void FUN_00265ec0_destroy_entity(std::size_t slot, const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }
    FUN_00265ec0_destroy_entity(slot, *environment.entityPool, environment.DAT_00343888_lights);
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

  // FUN_002d73e8 (0x002d73e8), the behaviour of type 0x192 -- **the target
  // cursor**, the marker drawn over an enemy while a battle is running.
  //
  // One is spawned per bound actor record by FUN_002d86b0 the frame the
  // pre-battle countdown reaches zero, and +0x19A names the pool slot it rides.
  // Everything below is the "+0x19A names somebody else" half; the other half,
  // taken when +0x19A names the cursor's own slot, is the scripted set-piece
  // marker of FUN_002d8808 and has nothing to do with targeting.
  //
  //   +0x04 |= 0x100          physics off: the cursor is placed, never simulated
  //   +0x08  = (h & 0xFBFE) | 0x4080   flat lighting, no rotation branch
  //   +0x14C = +0x150 = 1.0   scale reset every frame
  //   position = target +0x20/+0x24, and +0x28 + 0.75 * the target's height,
  //              plus the cursor's own +0x1A0..+0x1A8 offset and the target's
  //              hit-volume centre z
  //
  // Then it projects that point and **stores the screen position back into
  // +0x20/+0x24**, in pixels: (gsX >> 4) + 320 and (gsY >> 3) + 220, with the
  // projected depth word in +0x28 and +0x08 bit 0x1000 raised. That bit is what
  // tells FUN_0020f510 the position is already in screen space, and this is the
  // only entity in either scene that takes that branch.
  //
  // The animations are the whole of the selection feedback: 10 unselected, 12
  // the grow-in played when this cursor becomes the target, 11 selected, 13 the
  // shrink-in it spawns with. 12 and 13 both fall through to their resting
  // state when the clip ends.
  void FUN_002d73e8_target_cursor(OriginalEntity &cursor,
                                  std::size_t slot,
                                  const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    const std::int32_t targetSlot = cursor.cursorTarget19a;
    if (targetSlot < 0 || static_cast<std::size_t>(targetSlot) >= kEntitySlotCount ||
        static_cast<std::size_t>(targetSlot) == slot)
    {
      // FUN_002d8808's scripted-marker branch. Not reached by a battle cursor.
      return;
    }
    const auto &target = pool.slot(static_cast<std::size_t>(targetSlot));

    // :92-96.
    cursor.halfword04 |= 0x100u;
    cursor.flags06 = static_cast<std::uint16_t>(cursor.flags06 & 0xFFEFu);
    cursor.halfword08 = static_cast<std::uint16_t>((cursor.halfword08 & 0xFBFEu) | 0x4080u);
    cursor.scale14c = 1.0f;
    cursor.scaleZ150 = 1.0f;

    // :103-118. The point the marker sits on, then the target's hit-volume
    // centre z folded in on top of it.
    cursor.positionX20 = target.positionX20 + cursor.cursorOffsetX1a0;
    cursor.positionZ24 = target.positionZ24 + cursor.cursorOffsetY1a4;
    const float lift =
        cursor.cursorOffsetZ1a8 == 0.0f ? target.height58 * 0.75f : cursor.cursorOffsetZ1a8;
    const float worldZ = target.positionY28 + lift + target.hitVolumeOffset110[2];
    cursor.positionY28 = worldZ;
    const std::uint8_t flags198OnEntry = cursor.cursorFlags198;
    cursor.cursorFlags198 = static_cast<std::uint8_t>(flags198OnEntry & 0xFDu);

    // :119-122 and :288-292. **The two gates that decide whether a cursor is
    // shown at all**, both of which fall to the same tail: +0x198 non-zero, so
    // +0x08 bit 0 goes up and the sprite pass drops it.
    //
    //   DAT_00354FC2 & 5 must read exactly 1 -- the battle running (bit 0) and
    //   *not* suspended (bit 2, which only opcode 0xBD method 0x76 raises). Bit
    //   1, the "a battle is built" bit FUN_002432D8 sets, is deliberately not in
    //   the mask. Failing it takes +0x198 bit 1.
    //   DAT_0031DA6C's bit 0x20 on the driven member hides them too, and that
    //   one takes bit 2 instead.
    //
    // DAT_003555C6, the third term, is the attract-mode demo flag: FUN_00271558
    // raises it when the title screen times out and FUN_00271220 clears it on
    // every scene load. The port has no attract mode, so it is zero by
    // construction rather than by a read.
    if ((environment.DAT_00354fc2_battleState & 5u) != 1u)
    {
      cursor.cursorFlags198 = static_cast<std::uint8_t>((flags198OnEntry & 0xFDu) | 2u);
      cursor.halfword08 |= 1u;
      return;
    }
    cursor.cursorFlags198 = static_cast<std::uint8_t>(flags198OnEntry & 0xF9u);
    if (environment.DAT_0031d3c8_battleTableWord)
    {
      const std::uint32_t memberFlags = environment.DAT_0031d3c8_battleTableWord(
          orphen::ported::battle::kDAT_0031da6c_memberFlags);
      if ((memberFlags & 0x20u) != 0)
      {
        cursor.cursorFlags198 = static_cast<std::uint8_t>((flags198OnEntry & 0xF9u) | 4u);
        cursor.halfword08 |= 1u;
        return;
      }
    }

    // :198-205. The battle is over: the cursor shrinks out and stops drawing.
    // DAT_0031d7be is control block 0's +0x0E, and 0x0B is the action every
    // member is put into when the fight ends.
    ActorEnvironment::BattleMemberView member;
    const bool haveMember =
        environment.DAT_0031d7b0_battleMember && environment.DAT_0031d7b0_battleMember(0, member);
    if (haveMember && member.pendingAction0e == 0x0B)
    {
      FUN_00225bc8_set_animation(cursor, 13);
      cursor.halfword08 = static_cast<std::uint16_t>((cursor.halfword08 & 0xFBFFu) | 1u);
      cursor.flags06 |= 0x10u;
      cursor.cursorFlags198 |= 2u;
      return;
    }

    // :210-213. The target died; the cursor goes with it.
    if (static_cast<std::int16_t>(target.staggerTimer12a) < 1)
    {
      pool.releaseSlot(slot);
      return;
    }

    // :123-197. The projection. The original stages the point through VU0 and
    // reads back integer GS units; the port asks the runtime for the same
    // numbers. Off screen sets +0x198 bit 0, which the draw pass honours.
    if (!environment.FUN_0020b600_project)
    {
      return;
    }
    ActorEnvironment::ProjectedPoint projected;
    const orphen::ported::psm2::Vec3 world{cursor.positionX20, cursor.positionZ24, worldZ};
    // :148. The clear comes *before* FUN_0020b600, and every rejection below is
    // an OR into the same bit.
    cursor.cursorFlags198 = static_cast<std::uint8_t>(cursor.cursorFlags198 & 0xFEu);
    // :163-166 and :192-195. **A rejected point is not an early exit.** The VU0
    // divide is clamped, so FUN_0020b600 always writes a screen position back,
    // and the two depth tests either side of it -- w over DAT_0035479c, and the
    // pre-divide z at or under DAT_003547a0 (0.3) -- only raise +0x198 bit 0 and
    // fall through to the tail, which is where +0x08 bit 0 stops the draw.
    // Returning here instead left the cursor drawn at the last position it
    // projected to: s14_e031's narration close-up looks away from the target
    // dummy, so the marker sat in the corner of the screen for the whole of it.
    //
    // The port skips the position stores rather than reproducing the clamped
    // numbers -- vf1's minimum is part of the VU register bank the caller sets
    // up, not of this function -- which changes nothing on screen, because a
    // cursor that reaches the tail with +0x198 set does not draw.
    const bool projectedOntoScreen = environment.FUN_0020b600_project(world, projected);
    if (!projectedOntoScreen)
    {
      cursor.cursorFlags198 |= 1u;
    }
    // :151-162. The GS origin is 0x8000 in both axes, and the subtraction is
    // biased so the arithmetic shift that follows truncates toward zero rather
    // than down: -0x7FF1 is -0x8000 + 15 for the >> 4, -0x7FF9 is -0x8000 + 7
    // for the >> 3. X divides by 16 and Y by 8 because the GS output is 2:1.
    if (projectedOntoScreen)
    {
      const std::int32_t rawX = projected.gsX - 0x8000;
      const std::int32_t rawY = projected.gsY - 0x8000;
      const std::int32_t biasedX = rawX >= 0 ? rawX : projected.gsX - 0x7FF1;
      const std::int32_t biasedY = rawY >= 0 ? rawY : projected.gsY - 0x7FF9;
      cursor.cursorScreenX1ac = static_cast<float>(biasedX >> 4);
      cursor.cursorScreenY1b0 = static_cast<float>(biasedY >> 3);
      cursor.cursorScreenZ1b4 = static_cast<float>(projected.gsZ);
      const float screenX = static_cast<float>((biasedX >> 4) + 320);
      const float screenY = static_cast<float>((biasedY >> 3) + 220);
      cursor.positionX20 = screenX;
      cursor.positionZ24 = screenY;
      cursor.cursorProjectedDepth28 = projected.gsZ;
      cursor.halfword08 |= 0x1000u;

      // :180-198. The window the original rejects on, in pixels.
      if (screenX < 0.0f || screenX > 640.0f || screenY < 0.0f || screenY > 440.0f)
      {
        cursor.cursorFlags198 |= 1u;
      }
    }

    // :215-238. **The rotation.** +0x08 bit 0x400 switches the sprite pass from
    // the axis-aligned quad to FUN_0020F510's rotated-corner branch, and the
    // seven angles at +0x168 are what it turns by.
    //
    // A *selected* cursor -- animation 11 -- is pinned at DAT_003547A4, which is
    // pi/4 exactly: the bracket sits with a corner pointing up. Every other
    // cursor accumulates DAT_003555BC * DAT_003547A8 instead, 0.0002269 radians
    // a tick, so at the nominal 0x20 ticks a frame it turns about 25 degrees a
    // second -- the slow drift the unselected brackets have.
    //
    // Note the two loops fill different amounts: the pinned one writes all seven
    // slots, the spin writes six and leaves +0x180 alone. Reproduced as written.
    cursor.halfword08 |= 0x400u;
    if (cursor.animationA0 == 11)
    {
      for (int i = 6; i >= 0; --i)
      {
        cursor.spriteAngle168[i] = kDAT_003547a4_selectedCursorAngle;
      }
    }
    else
    {
      cursor.spriteAngle168[0] += static_cast<float>(environment.frameTicks) *
                                  kDAT_003547a8_cursorSpinRate;
      cursor.spriteAngle168[0] =
          orphen::ported::model::FUN_00216690_wrap_angle(cursor.spriteAngle168[0]);
      for (int i = 5; i >= 0; --i)
      {
        cursor.spriteAngle168[i] = cursor.spriteAngle168[0];
      }
    }

    // :239-259. Selection. The player's target is control block 0's +0x2C, a
    // pool slot; a cursor riding that slot plays the grow-in and then sits on
    // 11, and every other cursor goes back to 10.
    const std::int32_t playerTarget = haveMember ? member.target : -1;
    if (playerTarget > 0)
    {
      if (targetSlot == playerTarget)
      {
        if (cursor.animationA0 == 10)
        {
          cursor.flags06 = static_cast<std::uint16_t>(cursor.flags06 & 0xFFEFu);
          FUN_00225bc8_set_animation(cursor, 12);
          // :248. The selection chime, and the one sound in the battle HUD.
          // It is FUN_002057c8 directly rather than FUN_00267d38, so it is
          // never placed at the enemy: a flat cue at 0x80 on both channels,
          // one volume step above the 0x7F FUN_00267d38 would have used. It
          // fires on the *transition* only -- holding the D-pad on the same
          // target leaves the cursor on 12 and plays nothing.
          if (environment.FUN_002057c8_keyOn)
          {
            environment.FUN_002057c8_keyOn(kDAT_002d73e8_selectCue, 0x80, 0x80);
          }
        }
      }
      else if (cursor.animationA0 != 10)
      {
        cursor.flags06 = static_cast<std::uint16_t>(cursor.flags06 & 0xFFEFu);
        FUN_00225bc8_set_animation(cursor, 10);
      }
    }

    // :261-266 and :283-285. DAT_00354ECC hides every cursor outright;
    // otherwise bit 0x40 of +0x08 is the brightener the D-pad's 120-frame
    // display timer drives.
    if (environment.DAT_00354ecc_battleSuspended != 0)
    {
      cursor.halfword08 |= 1u;
      cursor.cursorFlags198 |= 0x40u;
      return;
    }
    if ((cursor.cursorFlags198 & 0x40u) == 0)
    {
      cursor.cursorFlags198 = static_cast<std::uint8_t>(cursor.cursorFlags198 & 0xEFu);
      if (environment.DAT_00354e96_targetDisplayTicks == 0)
      {
        cursor.halfword08 = static_cast<std::uint16_t>(cursor.halfword08 & 0xFFBFu);
      }
      else
      {
        // While the display is up, every cursor that is *not* the target takes
        // +0x198 bit 4 -- and that, through the tail below, is what hides it.
        // So the 120 frames after a D-pad step show one marker, not five.
        if (targetSlot != playerTarget)
        {
          cursor.cursorFlags198 |= 0x10u;
        }
        cursor.halfword08 |= 0x40u;
      }

      // :322-345. The two transient clips resolve when their animation ends;
      // +0x06 bit 0 is the clip-finished flag FUN_00225c90 raises.
      if (cursor.animationA0 == 12 && (cursor.flags06 & 1u) != 0)
      {
        FUN_00225bc8_set_animation(cursor, 11);
      }
      else if (cursor.animationA0 == 13 && (cursor.flags06 & 1u) != 0)
      {
        FUN_00225bc8_set_animation(cursor, 10);
      }
    }
    else
    {
      cursor.cursorFlags198 = static_cast<std::uint8_t>(cursor.cursorFlags198 & 0xBFu);
    }

    // :347-370. **+0x08 bit 0 is what stops the draw**, and it is raised only
    // when +0x198 came out of all of the above non-zero -- off screen, hidden
    // by the display, or retired. A cursor that reaches here with a clear
    // +0x198 leaves bit 0 down and FUN_0020f3e0's pass picks it up, which is
    // the whole reason the type carries +0x02 bit 0x200: it is a sprite, and
    // FUN_0020c5a8's model walk is meant to refuse it.
    if (cursor.cursorFlags198 != 0)
    {
      cursor.halfword08 |= 1u;
    }
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
    // **FUN_00305130 is cosf and FUN_00305218 is sinf.** +0x1A8 takes the
    // cosine and becomes +0x30, the X delta; +0x1AC takes the sine and becomes
    // +0x34, the Z delta. Swapping them mirrors the whole volley about the 45
    // degree diagonal, which reads as the fireballs flying off sideways or
    // backwards no matter where the caster is pointing.
    ball.fireballVelX1a8 = ball.fireballSpeed1bc * std::cos(ball.facingRadians5c);
    ball.fireballVelZ1ac = ball.fireballSpeed1bc * std::sin(ball.facingRadians5c);
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

  // ------------------------------------------------------- Bite of Lightning
  //
  // Circle's spell, the kind < 0 arm. Its chain is longer than Hand of Pyro's
  // and its shape is different: the fire spell throws a projectile, this one
  // marks a spot on the ground while it charges and then hits everything
  // standing on it.
  //
  //   FUN_0024c538  state 113, the hold     -> the aim marker, then the charge
  //   FUN_0024c910  state 114, the release  -> effect +0x60 = 1, +0x94 = level
  //   FUN_002deae8  type 0x174, the hand    -> FUN_002de650 on +0x60 == 1
  //   FUN_002de650  the launch              -> one 0x15C damage disc, one 0x178
  //                                            flash, one 0x15C spark per victim
  //
  // **Three separate entities are easy to confuse here.** The ring at the
  // caster's *feet* is type 0x18F (FUN_002d9c88, the charge gauge). The effect
  // in the caster's *hand* is this 0x174. The blue circle that grows on the
  // ground at the *landing spot* is neither -- it is the one shared hit effect,
  // type 0x1E3, moved and resized every frame by FUN_002f1380.

  // FUN_002de9e8 (0x002de9e8): one spark on a victim. A plain type 0x15C at a
  // fixed 2.0 scale, planted on the victim's own position -- the launch spawns
  // one of these for every entity its box caught.
  std::int32_t FUN_002de9e8_spawn_victim_spark(std::int8_t level,
                                               std::int16_t victimSlot,
                                               std::uint32_t hitParameters,
                                               std::int16_t casterSlot,
                                               float positionX,
                                               float positionZ,
                                               float positionY,
                                               const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr || environment.descriptors == nullptr)
    {
      return -1;
    }
    const std::size_t spawned = environment.entityPool->FUN_00265e28_allocate_and_initialize(
        0x15C, *environment.descriptors);
    if (spawned >= kEntitySlotCount)
    {
      return -1;
    }
    auto &spark = environment.entityPool->slot(spawned);
    spark.positionX20 = positionX;
    spark.positionZ24 = positionZ;
    spark.positionY28 = positionY;
    spark.groundHeight4c = positionY;
    spark.previousGroundHeight50 = positionY;
    spark.attackPower12c = 0;
    spark.descriptorFlags02 = static_cast<std::uint16_t>(spark.descriptorFlags02 | 0x1000u);
    spark.halfword04 = 0x19;
    spark.hitParameters198 = hitParameters;
    spark.lightningTarget1ac = victimSlot;
    spark.lightningCaster1ae = casterSlot;
    spark.lightningLevel1b3 = (level == 0) ? static_cast<std::int8_t>(1) : level;
    spark.lightningByte1b2 = 0;
    spark.fadeRamp62 = 0;
    spark.animationA0 = 0;
    spark.lightningTimer1b0 = static_cast<std::uint16_t>(FUN_00248e48_arm_timer(0x20));
    spark.scale14c = 2.0f;
    spark.scaleZ150 = 2.0f;
    return static_cast<std::int32_t>(spawned);
  }

  // FUN_002de650 (0x002de650): the launch. Everything the spell does on the
  // frame the release animation reaches its marker.
  //
  // **The summon is the first branch and it is not ported.** At level 5 with a
  // live target the whole thing hands over to FUN_002deef0, which interrupts
  // the battle to play a spirit summon and damage every enemy. Its entry point
  // is written up in docs/bite_of_lightning_spell_path.md; nothing else here
  // depends on it, and with no enemy table the target stays -1, so the branch
  // is unreachable in this slice.
  //
  // Both spawns are allocated as type **0x174** and then have their +0x00
  // overwritten -- 0x15C for the damage disc, 0x178 for the flash. They get
  // 0x174's descriptor and model but 0x15C's and 0x178's behaviour. The port
  // resolves the handler from typeId00 every frame, so that works; caching a
  // handler at spawn time would break it silently.
  // FUN_002DEEF0, the creature this hands over to at full charge. Its body is
  // with the other four summons below.
  std::int32_t FUN_002deef0_spawn_bite_summon(std::uint16_t attackPower,
                                              std::int16_t target,
                                              std::uint32_t hitParameters,
                                              const orphen::ported::psm2::Vec3 &anchor,
                                              std::int16_t casterSlot,
                                              std::uint8_t level,
                                              const ActorEnvironment &environment);

  std::int32_t FUN_002de650_launch_lightning(std::uint8_t level,
                                             std::uint16_t attackPower,
                                             std::int16_t target,
                                             std::uint32_t hitParameters,
                                             std::int16_t casterSlot,
                                             const orphen::ported::psm2::Vec3 &summonAnchor,
                                             const orphen::ported::psm2::Vec3 &castPosition,
                                             const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr || environment.descriptors == nullptr)
    {
      return 0;
    }
    EntityPool &pool = *environment.entityPool;

    if (level == 5 && target > 1)
    {
      // FUN_002DEEF0: the creature, anchored at the caster's hand. The launch
      // returns nothing, the same way the other four do.
      FUN_002deef0_spawn_bite_summon(attackPower, target, hitParameters, summonAnchor, casterSlot,
                                     level, environment);
      return 0;
    }

    // FUN_0023bbd8(0, 7, level, power) when the player cast it: the pad rumble,
    // DAT_00571B50's four slots. The port has no rumble path.

    std::int32_t scaled = (level == 0) ? 1 : static_cast<std::int32_t>(level);
    if (scaled > 5)
    {
      scaled = 5;
    }
    const float levelF = static_cast<float>(scaled);
    const std::int8_t levelB = static_cast<std::int8_t>(scaled);

    // ---- (a) the damage disc, type 0x15C -----------------------------------
    const std::size_t discSlot =
        pool.FUN_00265e28_allocate_and_initialize(0x174, *environment.descriptors);
    if (discSlot >= kEntitySlotCount)
    {
      return 0;
    }
    auto &disc = pool.slot(discSlot);
    disc.typeId00 = 0x15C;
    disc.modelTypeId15c = 0x174; // allocated as 0x174; the model stays 0x174's
    disc.depthBias133 = static_cast<std::int8_t>(levelB * -0x0C);
    disc.facingRadians5c = (static_cast<std::size_t>(casterSlot) < kEntitySlotCount)
                               ? pool.slot(static_cast<std::size_t>(casterSlot)).facingRadians5c
                               : 0.0f;
    disc.descriptorFlags02 = static_cast<std::uint16_t>(disc.descriptorFlags02 | 0x1000u);
    disc.halfword04 = 0x19;
    disc.positionX20 = castPosition.x;
    disc.positionZ24 = castPosition.y;
    disc.positionY28 = castPosition.z;
    disc.groundHeight4c = castPosition.z;
    disc.previousGroundHeight50 = castPosition.z;
    disc.attackPower12c = static_cast<std::uint16_t>(attackPower + scaled * 4);
    disc.hitParameters198 = hitParameters;
    disc.lightningTarget1ac = target;
    disc.lightningCaster1ae = casterSlot;
    disc.lightningLevel1b3 = levelB;
    disc.lightningByte1b2 = 0;
    disc.fadeRamp62 = 0;
    disc.animationA0 = 0;
    disc.scale14c = levelF * 2.0f;
    disc.scaleZ150 = 0.3f; // DAT_00354914
    disc.lightningTimer1b0 = static_cast<std::uint16_t>(FUN_00248e48_arm_timer(0x20));
    FUN_00215e48_clear_hit_set(disc);
    if (casterSlot == 0 && target > 2 && static_cast<std::size_t>(target) < kEntitySlotCount &&
        (pool.slot(static_cast<std::size_t>(target)).effectFlags96 & 0x40u) != 0)
    {
      // +0x96 bit 0x40: the player's own instant-kill path through the hit
      // test, carried over from the target only when the player cast it.
      disc.effectFlags96 = static_cast<std::uint8_t>(disc.effectFlags96 | 0x40u);
    }

    // The box is level-sized: 1.5 per level horizontally, 0.5 vertically,
    // centred on the landing spot. At level 5 that is a 15-unit square.
    std::int8_t contacts = 0;
    if (environment.hitTest != nullptr)
    {
      const float half = levelF * 1.5f;
      const float halfY = levelF * 0.5f;
      const std::array<float, 6> box{castPosition.x - half,  castPosition.x + half,
                                     castPosition.y - half,  castPosition.y + half,
                                     castPosition.z - halfY, castPosition.z + halfY};
      contacts = FUN_00215ac8_box_hit_test(
          disc, discSlot, box, orphen::ported::resource::HitParameters::unpack(hitParameters),
          *environment.hitTest);
    }
    if (environment.FUN_00267d38_playSound)
    {
      environment.FUN_00267d38_playSound(0xE1, disc); // the thunder crack
    }

    // ---- (b) the flash, type 0x178 -----------------------------------------
    std::int32_t flashSlot = 0;
    const std::size_t flash =
        pool.FUN_00265e28_allocate_and_initialize(0x174, *environment.descriptors);
    if (flash < kEntitySlotCount)
    {
      auto &burst = pool.slot(flash);
      burst.typeId00 = 0x178;
      burst.modelTypeId15c = 0x174;
      burst.attackPower12c = 0;
      burst.depthBias133 = static_cast<std::int8_t>(levelB * -0x0C);
      burst.descriptorFlags02 = static_cast<std::uint16_t>(burst.descriptorFlags02 | 0x1000u);
      burst.halfword04 = 0x19;
      burst.positionX20 = castPosition.x;
      burst.positionZ24 = castPosition.y;
      burst.positionY28 = castPosition.z;
      burst.groundHeight4c = castPosition.z;
      burst.previousGroundHeight50 = castPosition.z;
      burst.hitParameters198 = hitParameters;
      burst.lightningLevel1b3 = levelB;
      burst.lightningByte1b2 = 0;
      burst.lightningTarget1ac = target;
      burst.lightningCaster1ae = casterSlot;
      // Animation 3, or 4 while DAT_00354ecc -- the battle opener's hold. That
      // word is 0 in the ELF and the port keeps it that way.
      burst.animationA0 = 3;
      burst.fadeRamp62 = 0;
      burst.lightningTimer1b0 = static_cast<std::uint16_t>(FUN_00248e48_arm_timer(0x20));
      flashSlot = static_cast<std::int32_t>(flash);
    }

    // ---- (c) one spark per victim ------------------------------------------
    if (contacts == 0 || environment.hitTest == nullptr ||
        environment.hitTest->DAT_003151c8_hitList == nullptr)
    {
      return flashSlot;
    }
    const auto &hitList = *environment.hitTest->DAT_003151c8_hitList;
    // The original walks DAT_003151C8 until a non-positive entry and gives up
    // after index 0x14, so at most 21 sparks.
    const std::size_t limit = std::min<std::size_t>(hitList.size(), 21u);
    for (std::size_t index = 0; index < limit; ++index)
    {
      const std::uint16_t victim = hitList[index];
      if (victim == 0 || static_cast<std::size_t>(victim) >= kEntitySlotCount)
      {
        break;
      }
      const auto &body = pool.slot(static_cast<std::size_t>(victim));
      FUN_002de9e8_spawn_victim_spark(levelB, static_cast<std::int16_t>(victim), hitParameters,
                                      casterSlot, body.positionX20, body.positionZ24,
                                      body.positionY28, environment);
    }
    return flashSlot;
  }

  // FUN_002deae8 (0x002deae8), the behaviour of type 0x174 -- the charge that
  // gathers in Orphen's hand while Circle is held.
  //
  // Structurally the twin of FUN_002da8a0 above, with one line the fire spell
  // has no equivalent of: **every frame it is drawing, it calls FUN_002f1380**,
  // which moves the shared 0x1E3 hit effect to wherever the spell would land
  // and scales it by the charge. That is the blue circle that grows on the
  // ground under the target. It runs only for the player -- `caster == 0` --
  // and it reads pool slot 0's charge directly rather than its own caster's,
  // which is the original's own hardcoding of 0x58BEB0.
  void FUN_002deae8_lightning_hand(OriginalEntity &effect,
                                   std::size_t /*slot*/,
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
    effect.depthBias133 = -0x0C; // 0xF4
    effect.halfword08 = static_cast<std::uint16_t>(entryFlags08 | 0x4000u);
    effect.facingRadians5c = 0.0f;

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
      // `1 < (byte)(current + 0x74)` -- true for anything outside the two
      // action bytes 0x8C and 0x8D, i.e. the caster has stopped casting.
      const std::uint8_t action = haveBlock ? view.currentAction0f : 0;
      if (static_cast<std::uint8_t>(action + 0x74u) > 1u)
      {
        effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 | 1u);
        FUN_00225bc8_set_animation(effect, 2);
      }
      step = static_cast<std::int16_t>(effect.animationA0);
    }

    if (step == 0 || step == 1)
    {
      // **The targeting circle.** FUN_00249270(pool[0], 1) is the raw charge
      // capped at 0x2580, so the scale runs 1.5 at a tap to 11.1 at a full
      // charge. The height stays 1.0; only the radius grows.
      if (casterSlot == 0 && environment.FUN_002f1380_show_hit_effect &&
          environment.FUN_002493f0_spell_landing)
      {
        ActorEnvironment::BattleMemberView lead;
        const OriginalEntity &player = pool.slot(0);
        std::int32_t charge = 0;
        if (player.byte95 != 0 && environment.DAT_0031d7b0_battleMember &&
            environment.DAT_0031d7b0_battleMember(static_cast<std::uint32_t>(player.byte95) - 1u,
                                                  lead))
        {
          charge = lead.chargeTimer3c > 0x2580 ? 0x2580 : lead.chargeTimer3c;
        }
        orphen::ported::psm2::Vec3 landing{};
        environment.FUN_002493f0_spell_landing(0, landing);
        environment.FUN_002f1380_show_hit_effect(static_cast<float>(charge) / 1000.0f + 1.5f, 1.0f,
                                                 landing);
      }
      // The hand light. Blue where Hand of Pyro's is orange, and twice its base
      // radius.
      FUN_002da220_spell_light(effect, casterSlot, haveBlock ? view.chargeTimer3c : 0, 0x54, 0x8D,
                               0xFE, 1000, environment);
    }

    if (step == 1)
    {
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
      effect.flags06 = static_cast<std::uint16_t>(effect.flags06 & 0xFFEFu);
      effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 & 0xFFFEu);
    }
    else if (step == 2)
    {
      if ((effect.flags06 & 1u) != 0)
      {
        effect.parentSlot192 = casterIndex;
        effect.flags06 = static_cast<std::uint16_t>(effect.flags06 | 0x10u);
        effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 | 1u);
        effect.scale14c = 1.0f;
        effect.scaleZ150 = 1.0f;
        // FUN_00266098: give the light slot back.
        if (effect.lightSlot195 >= 0 && environment.DAT_00343888_lights != nullptr)
        {
          environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(effect.lightSlot195))
              .radius = 0.0f;
          effect.lightSlot195 = -1;
        }
      }
    }

    // **The launch.** +0x60 is 1 for exactly one frame, written by FUN_0024c910
    // when the release animation reaches its +0xAA bit 0x100 marker.
    if (effect.state60 != 1)
    {
      return;
    }
    effect.flags06 = static_cast<std::uint16_t>(effect.flags06 & 0xFFEFu);
    effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 & 0xFFFEu);
    FUN_00225bc8_set_animation(effect, 2);
    effect.state60 = 0;

    // FUN_0020dc88(caster, +0x194, matrixOut, positionOut). Both outputs feed
    // FUN_002de650's param_6, which only the summon branch reads -- so this is
    // kept for shape, not for a value anything on this path consumes.
    // The summon's anchor -- param_6, which only the level-5 branch reads.
    orphen::ported::psm2::Vec3 summonAnchor{caster.positionX20, caster.positionZ24,
                                            caster.positionY28};
    if (environment.FUN_0020dc88_bone_point)
    {
      summonAnchor = environment.FUN_0020dc88_bone_point(
          casterSlot, static_cast<std::size_t>(effect.attachBone194),
          orphen::ported::psm2::Vec3{0.0f, 0.0f, 0.0f});
    }

    orphen::ported::psm2::Vec3 castPosition{caster.positionX20, caster.positionZ24,
                                            caster.positionY28};
    std::int32_t target = -1;
    if (environment.FUN_002493f0_spell_landing)
    {
      target = environment.FUN_002493f0_spell_landing(casterSlot, castPosition);
    }
    // On the hand effect +0x198 is a pointer into the caster's party record;
    // FUN_00267da0 copies the four bytes it names onto each spawn as a value.
    const std::uint32_t hitParameters =
        environment.DAT_0031d3c8_battleTableWord
            ? environment.DAT_0031d3c8_battleTableWord(effect.hitParameters198)
            : 0;
    FUN_002de650_launch_lightning(effect.spawnParam94, effect.attackPower12c,
                                  static_cast<std::int16_t>(target), hitParameters,
                                  static_cast<std::int16_t>(casterSlot), summonAnchor, castPosition,
                                  environment);
  }


  // ================================ the four other kind -2 elemental spells
  //
  // Falcon of Death, Hammer of Evil, Pinnacle of the Sun and Hail of Heavens
  // are Bite of Lightning four more times. Each is a hand effect, a launch and
  // a projectile, and the three functions are near-clones of 0x174's -- a
  // structural diff of FUN_002dfd38 against FUN_002e2048 comes back as two
  // constants and a label name. So they are one body and a five-row table
  // rather than four copies, and the row is the only place an element differs.
  //
  //   spell 8  Falcon of Death        0x175  FUN_002e3110 / FUN_002e2d38
  //   spell 9  Hammer of Evil         0x177  FUN_002e2048 / FUN_002e1d20
  //   spell 10 Pinnacle of the Sun    0x179  FUN_002dfd38 / FUN_002dfb40
  //   spell 11 Hail of Heavens        0x17C  FUN_002e0f80 / FUN_002e0c68
  //   (spell 7 Bite of Lightning      0x174  FUN_002deae8 / FUN_002de650, above)
  //
  // The four differ from 0x174 in three places, all of them in the hand:
  // they negate +0x194 so the bone index is always taken positive, they zero
  // their own position and copy the caster's facing into +0x5C rather than
  // leaving it at zero, and their launch takes a leading element byte 0x174's
  // does not.
  struct ElementalSpellB
  {
    std::int16_t handType;
    std::uint8_t lightRed;
    std::uint8_t lightGreen;
    std::uint8_t lightBlue;
    // The launch allocates its projectile as `handType` -- so it gets the
    // hand's descriptor and model -- and then overwrites +0x00 with this.
    std::int16_t projectileType;
    // Falcon alone folds the charge level into the projectile's attack power;
    // the other three pass the party record's number through.
    bool foldLevelIntoPower;
    std::uint16_t projectileAnimation;
    // Falcon and Hammer scale the projectile with the charge; the other two
    // leave it at 1.0.
    bool scaleWithLevel;
  };

  inline constexpr ElementalSpellB kElementalSpellB[4]{
      {0x175, 0x46, 0x46, 0xAA, 0x15D, true, 3, true},   // Falcon of Death
      {0x177, 0xAA, 0x00, 0xAA, 0x156, false, 3, true},  // Hammer of Evil
      {0x179, 0xAA, 0x0A, 0x0A, 0x158, false, 4, false}, // Pinnacle of the Sun
      {0x17C, 0x00, 0xAA, 0xAA, 0x15A, false, 4, false}, // Hail of Heavens
  };

  const ElementalSpellB *elementalSpellBForHand(std::int16_t typeId)
  {
    for (const auto &row : kElementalSpellB)
    {
      if (row.handType == typeId)
      {
        return &row;
      }
    }
    return nullptr;
  }

  // fGpffffa9e4 / fGpffffa9d0, both 0.4: the charge's share of the
  // projectile's scale, which runs 1.4 at a tap to 3.0 at a full charge.
  inline constexpr float kFGpffffa9e4_projectileScalePerLevel = 0.4000000059604645f;

  // FUN_002e9668 (0x002e9668), shared by all four launches: sweep a
  // level-sized box through the hit test and plant one spark on every victim.
  //
  // The box is the same shape 0x174's launch builds inline -- 1.5 per level
  // across, 0.5 up -- and the sparks are the same type 0x15C, allocated with
  // the element's own type so they carry its model. The one thing that is not
  // in FUN_002de650: at level 5 the hit record's reaction byte is temporarily
  // forced to 0x18 for the duration of the sweep and then put back.
  void FUN_002e9668_elemental_sweep(std::int16_t elementType,
                                    std::int32_t level,
                                    std::uint32_t hitParameters,
                                    OriginalEntity &projectile,
                                    std::size_t projectileSlot,
                                    const orphen::ported::psm2::Vec3 &castPosition,
                                    const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr || environment.descriptors == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;

    FUN_00215e48_clear_hit_set(projectile);
    if (environment.FUN_00267d38_playSound)
    {
      environment.FUN_00267d38_playSound(0xE1, projectile);
    }

    std::int8_t contacts = 0;
    if (environment.hitTest != nullptr)
    {
      auto parameters = orphen::ported::resource::HitParameters::unpack(hitParameters);
      if (level > 4)
      {
        parameters.reaction = 0x18;
      }
      const float levelF = static_cast<float>(level);
      const float half = levelF * 1.5f;
      const float halfY = levelF * 0.5f;
      const std::array<float, 6> box{castPosition.x - half,  castPosition.x + half,
                                     castPosition.y - half,  castPosition.y + half,
                                     castPosition.z - halfY, castPosition.z + halfY};
      contacts =
          FUN_00215ac8_box_hit_test(projectile, projectileSlot, box, parameters, *environment.hitTest);
    }
    // FUN_0023bbd8(0, 7): the pad rumble. No rumble path in the port.

    if (contacts == 0 || environment.hitTest == nullptr ||
        environment.hitTest->DAT_003151c8_hitList == nullptr)
    {
      return;
    }
    const auto &hitList = *environment.hitTest->DAT_003151c8_hitList;
    // The original stops on a non-positive entry and gives up past index 0x14.
    const std::size_t limit = std::min<std::size_t>(hitList.size(), 21u);
    for (std::size_t index = 0; index < limit; ++index)
    {
      const std::uint16_t victim = hitList[index];
      if (victim == 0 || static_cast<std::size_t>(victim) >= kEntitySlotCount)
      {
        break;
      }
      const auto &body = pool.slot(static_cast<std::size_t>(victim));
      const std::size_t sparkSlot = pool.FUN_00265e28_allocate_and_initialize(
          elementType, *environment.descriptors);
      if (sparkSlot >= kEntitySlotCount)
      {
        break;
      }
      auto &spark = pool.slot(sparkSlot);
      spark.typeId00 = 0x15C;
      spark.modelTypeId15c = elementType;
      spark.descriptorFlags02 = static_cast<std::uint16_t>(spark.descriptorFlags02 | 0x1000u);
      spark.halfword04 = 0x19;
      spark.attackPower12c = 0;
      spark.positionX20 = body.positionX20;
      spark.positionZ24 = body.positionZ24;
      spark.positionY28 = body.positionY28;
      spark.groundHeight4c = body.positionY28;
      spark.previousGroundHeight50 = body.positionY28;
      spark.animationA0 = 0;
      spark.lightningTimer1b0 = static_cast<std::uint16_t>(FUN_00248e48_arm_timer(0x20));
      spark.scale14c = 2.0f;
      spark.scaleZ150 = 2.0f;
    }
  }


  // ======================== the four level-5 summons
  //
  // Released at full charge with a live target, each of the four elemental
  // spells stops being a projectile and becomes a creature:
  //
  //   spell  8 Falcon of Death       0x13F  FUN_002E34B8  spawned by FUN_002E2F50
  //   spell  9 Hammer of Evil        0x140  FUN_002E23E8  spawned by FUN_002E1F28
  //   spell 10 Pinnacle of the Sun   0x141  FUN_002E01F8  spawned by FUN_002E00D8
  //   spell 11 Hail of Heavens       0x142  FUN_002E1320  spawned by FUN_002E0E60
  //
  // The four bodies are three hundred lines apiece and near-clones of one
  // another; the spine they share is written out once below and each body then
  // does its own three things in its own order. What the spine is:
  //
  //  * **It takes the field.** The whole pool is frozen (SummonStage), a
  //    handful of entities are handed the bit back, and everything left in the
  //    dim set fades with the map's global cap. DAT_00354ECC goes to 1 for the
  //    creature's whole run -- which is what makes the spell-reward cutscene's
  //    beat wait for it, and the reason all four of those arms used to advance
  //    a frame after the cast instead of playing out.
  //  * **It rides the caster.** The creature does not move: every frame it
  //    copies the caster's position and facing, and the caster is turned one
  //    capped step toward the target. The two camera curves are sampled in the
  //    creature's frame -- the sample's length and angle are taken apart,
  //    the angle is added to the facing, and the result is put back -- so the
  //    same seven points read the same way whichever way the caster stands.
  //  * **It plays out on animation markers.** `(+0xAA & 0xF00)` naming 7, 6 and
  //    3 with +0x06 bit 4 up are the three shouts, and 3 is also where the
  //    damage goes off: the creature calls its own spell's launch back, at
  //    level 6 so the summon branch cannot recurse, from the *target's*
  //    position rather than the caster's. After that a timeline frame each
  //    spell names starts the creature fading, and one more ends it.
  //
  // Not ported, and named where they belong: the flat veil the stage sits
  // behind (see original_summon_stage.h) and DAT_0031DA1E, the first-time
  // spirit-name banner -- three of the four arm it, nothing in the executable
  // draws it, and the one function that reads it only counts its timer down.

  struct SummonSpell
  {
    std::int16_t summonType;     // +0x00 of the creature
    std::int16_t handType;       // the elemental hand whose launch it calls back
    std::uint16_t spawnAnimation; // +0xA0 the spawner gives it
    // The two camera curves, in the creature's own frame.
    std::span<const orphen::ported::psm2::Vec3> eyePoints;
    std::span<const orphen::ported::psm2::Vec3> lookAtPoints;
    std::int16_t curveDuration; // DAT_00355574 and DAT_00355576, always equal
    float turnRate;             // the caster's step toward the target, per tick
    // The timeline frame the creature starts fading on, and the one that ends
    // it. Both are read off +0xA8, which steps by two per six-byte entry.
    std::int16_t fadeFromFrame;
    std::int16_t endFrame;
    // DAT_0035554C's step on the way out. The stage comes back at three
    // different speeds and Falcon's is twice everyone else's.
    std::int32_t exitRate;
    // DAT_0031DA1C's bit and the banner id that goes with it, plus the frame
    // it fires on. Zero means this spell has no banner.
    std::int16_t bannerFrame;
    std::uint8_t bannerBit;
    std::uint8_t bannerId;
  };

  // DAT_0034FAE8 onward: four pairs of plain Vec3 runs, read straight out of
  // the executable the way the boss camera's shots are. Component 1 is the
  // original's z and component 2 its height, the same order every position in
  // the pool is stored in.
  inline constexpr orphen::ported::psm2::Vec3 kDAT_0034fbf8_falconLookAt[2]{
      {0.345f, -0.031f, 0.941f},
      {0.156f, -0.041f, 1.380f},
  };
  inline constexpr orphen::ported::psm2::Vec3 kDAT_0034fc10_falconEye[9]{
      {-0.075f, 1.474f, 0.075f}, {0.677f, 1.474f, 0.050f},   {1.411f, 0.972f, 0.075f},
      {1.537f, 0.094f, 0.025f},  {1.286f, -0.752f, 0.075f},  {-0.094f, -1.411f, 0.225f},
      {-1.254f, -0.407f, 0.803f}, {-1.756f, -0.062f, 0.878f}, {-2.572f, -0.125f, 2.534f},
  };
  inline constexpr orphen::ported::psm2::Vec3 kDAT_0034fbb8_hammerLookAt[1]{
      {1.104f, 0.167f, 1.405f},
  };
  inline constexpr orphen::ported::psm2::Vec3 kDAT_0034fbc8_hammerEye[4]{
      {3.363f, -0.083f, -0.175f},
      {3.087f, 1.422f, -0.125f},
      {1.798f, 1.882f, -0.075f},
      {-0.209f, 1.171f, 1.204f},
  };
  inline constexpr orphen::ported::psm2::Vec3 kDAT_0034fae8_pinnacleLookAt[2]{
      {0.397f, 0.000f, 2.675f},
      {0.972f, -0.041f, 1.241f},
  };
  inline constexpr orphen::ported::psm2::Vec3 kDAT_0034fb00_pinnacleEye[7]{
      {-2.279f, 0.962f, 0.611f}, {-0.920f, 1.547f, 0.025f}, {0.583f, 1.819f, 0.464f},
      {1.819f, 1.656f, 2.679f},  {2.221f, 0.158f, 3.120f},  {2.256f, -1.756f, 2.656f},
      {2.227f, -1.706f, -0.390f},
  };
  inline constexpr orphen::ported::psm2::Vec3 kDAT_0034fb58_hailLookAt[2]{
      {0.271f, 0.000f, 1.129f},
      {-0.313f, -0.041f, 1.524f},
  };
  inline constexpr orphen::ported::psm2::Vec3 kDAT_0034fb70_hailEye[6]{
      {-2.677f, 0.962f, 0.669f}, {-0.334f, 2.216f, 2.175f}, {1.798f, 1.379f, 2.718f},
      {1.589f, -0.752f, 3.118f}, {-0.376f, -1.505f, 2.593f}, {-2.551f, 0.000f, 2.006f},
  };

  // DAT_00354928 / DAT_00354934 / DAT_00354944 / DAT_0035495C. All four are the
  // same 0.00261799 -- an eighth of a degree a tick -- and they are four
  // separate words in the executable, so they get four slots here.
  inline constexpr float kSummonTurnRate = 0.0026179900858551f;

  // fGpffffa9e8, 0.2: the lift Falcon of Death gives the flash it drops at the
  // caster's feet.
  inline constexpr float kFGpffffa9e8_flashLift = 0.2000000029802322f;

  inline const SummonSpell kSummonSpells[4]{
      {0x13F, 0x175, 5, kDAT_0034fc10_falconEye, kDAT_0034fbf8_falconLookAt, 0x21C0,
       kSummonTurnRate, 0x2B, 0x2C, 0x12, 0x2A, 0x04, 2},
      {0x140, 0x177, 5, kDAT_0034fbc8_hammerEye, kDAT_0034fbb8_hammerLookAt, 0x1D88,
       kSummonTurnRate, 0x39, 0x3A, 3, 0x30, 0x20, 5},
      {0x141, 0x179, 4, kDAT_0034fb00_pinnacleEye, kDAT_0034fae8_pinnacleLookAt, 0x21C0,
       kSummonTurnRate, 0x33, 0x34, 9, 0x32, 0x10, 4},
      {0x142, 0x17C, 5, kDAT_0034fb70_hailEye, kDAT_0034fb58_hailLookAt, 0x21C0,
       kSummonTurnRate, 0x2F, 0x30, 9, 0, 0, 0},
  };

  const SummonSpell *summonForType(std::int16_t typeId)
  {
    for (const auto &row : kSummonSpells)
    {
      if (row.summonType == typeId)
      {
        return &row;
      }
    }
    return nullptr;
  }

  const SummonSpell *summonForHand(std::int16_t handType)
  {
    for (const auto &row : kSummonSpells)
    {
      if (row.handType == handType)
      {
        return &row;
      }
    }
    return nullptr;
  }

  // DAT_00355558 / DAT_0035555C / DAT_00355560 / DAT_00355564: the angle from
  // the caster to the target, latched on the frame the camera starts and turned
  // toward for the rest of the run. Four separate globals in the executable,
  // one per spell, and never two at once.
  float &DAT_00355558_summonFacing(const SummonSpell &spell)
  {
    static std::array<float, 4> facing{};
    return facing[static_cast<std::size_t>(&spell - kSummonSpells)];
  }

  // The three battle-owned entities the stage has to keep running, resolved
  // once so the four bodies do not each re-derive them.
  ActorEnvironment::SummonExemptSlots summon_exempt(const OriginalEntity &caster,
                                                    const ActorEnvironment &environment)
  {
    ActorEnvironment::SummonExemptSlots out{};
    if (caster.byte95 != 0 && environment.DAT_0031da8c_summonExempt)
    {
      environment.DAT_0031da8c_summonExempt(static_cast<std::uint32_t>(caster.byte95) - 1u, out);
    }
    return out;
  }

  // The +0x94 == 0 frame: freeze the field, build the dim set, and hand the bit
  // and the light back to the few things that keep running. The four bodies
  // permute these writes -- there is no read between any of them -- and only
  // Falcon leaves the camera's roll and zoom alone, because its own arrival
  // flips DAT_00343880 instead.
  void summon_enter(OriginalEntity &entity,
                    std::size_t slot,
                    const ActorEnvironment &environment,
                    bool resetRollAndZoom)
  {
    EntityPool &pool = *environment.entityPool;
    SummonStage &stage = DAT_0058bb00_summonStage();

    if (environment.DAT_00354ecc_setBattleSuspended)
    {
      environment.DAT_00354ecc_setBattleSuspended(1);
    }
    entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10u);

    const std::int16_t casterIndex = entity.lightningCaster1ae;
    const bool haveCaster = casterIndex >= 0 &&
                            static_cast<std::size_t>(casterIndex) < kEntitySlotCount;
    const ActorEnvironment::SummonExemptSlots exempt =
        haveCaster ? summon_exempt(pool.slot(static_cast<std::size_t>(casterIndex)), environment)
                   : ActorEnvironment::SummonExemptSlots{};

    SummonStage::FUN_002de4a8_freeze_field(pool);
    SummonStage::FUN_002de640_release_one(entity);
    SummonStage::FUN_002de640_release_one(pool, 0);
    SummonStage::FUN_002de640_release_one(pool, exempt.DAT_0031dad0_sharedHitEffect);

    // The caster's ground ring is driven to animation 3 -- its closed pose --
    // on the way in, so the cast circle is not still open under the creature.
    if (exempt.DAT_0031da8c_castRing >= 0 &&
        static_cast<std::size_t>(exempt.DAT_0031da8c_castRing) < kEntitySlotCount)
    {
      pool.slot(static_cast<std::size_t>(exempt.DAT_0031da8c_castRing)).animationA0 = 3;
    }
    stage.FUN_002d6f38_exclude(exempt.DAT_0031da8c_castRing);
    SummonStage::FUN_002de640_release_one(pool, exempt.DAT_0031da8c_castRing);

    stage.FUN_002d6e20_build_dim_set(pool);
    stage.FUN_002d6f38_exclude(static_cast<std::int32_t>(slot));
    stage.FUN_002d6f38_exclude(casterIndex);
    stage.FUN_002d6f38_exclude(exempt.DAT_0031daac_shield);
    SummonStage::FUN_002de640_release_one(pool, exempt.DAT_0031daac_shield);

    if (resetRollAndZoom && environment.camera != nullptr)
    {
      // uGpffffb6dc, fGpffffb6e8 and uGpffffb6ec. The third is written here and
      // by FUN_00217FE8 and read by nothing, so the port has no field for it.
      environment.camera->setRoll(0.0f);
      environment.camera->setZoomLog2(1.0f);
    }
  }

  // The +0x94 == 100 frame: install the two curves, latch the angle to the
  // target, and start both ramps. One frame, then the creature never touches
  // any of it again.
  void summon_begin_camera(OriginalEntity &entity,
                           const ActorEnvironment &environment,
                           const SummonSpell &spell)
  {
    EntityPool &pool = *environment.entityPool;
    SummonStage &stage = DAT_0058bb00_summonStage();

    const std::int16_t casterIndex = entity.lightningCaster1ae;
    if (casterIndex >= 0 && static_cast<std::size_t>(casterIndex) < kEntitySlotCount)
    {
      SummonStage::FUN_002de640_release_one(pool.slot(static_cast<std::size_t>(casterIndex)));
    }
    entity.flags06 = static_cast<std::uint16_t>(entity.flags06 & 0xFFEFu);
    entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 & 0xFFFEu);

    stage.FUN_00266a78_build_eye(spell.eyePoints);
    stage.FUN_00266a78_build_look_at(spell.lookAtPoints);
    if (environment.DAT_00355700_globalFadeCap != nullptr)
    {
      *environment.DAT_00355700_globalFadeCap = 0x7F;
    }
    stage.armCurves(spell.curveDuration, spell.curveDuration);
    entity.spawnParam94 = 1;
    stage.DAT_00355554_creatureFade() = 0x319C;
    stage.DAT_0035554c_stageFade() = 0x319C;
    if (environment.DAT_00343878_frameFeedback != nullptr)
    {
      // DAT_00355661 = 100, the alpha the smear starts at.
      environment.DAT_00343878_frameFeedback->FUN_00264448_set_alpha(100);
    }

    const std::int16_t targetIndex = entity.lightningTarget1ac;
    if (casterIndex >= 0 && static_cast<std::size_t>(casterIndex) < kEntitySlotCount &&
        targetIndex >= 0 && static_cast<std::size_t>(targetIndex) < kEntitySlotCount)
    {
      const OriginalEntity &caster = pool.slot(static_cast<std::size_t>(casterIndex));
      const OriginalEntity &target = pool.slot(static_cast<std::size_t>(targetIndex));
      DAT_00355558_summonFacing(spell) = std::atan2(target.positionZ24 - caster.positionZ24,
                                                    target.positionX20 - caster.positionX20);
    }
  }

  // Every frame after that: one capped step of the caster toward the latched
  // angle, then the creature copies the caster outright. `heightBias` is the
  // lift Falcon of Death alone gives its ground height.
  void summon_track_caster(OriginalEntity &entity,
                           const ActorEnvironment &environment,
                           const SummonSpell &spell,
                           float heightBias)
  {
    EntityPool &pool = *environment.entityPool;
    const std::int16_t casterIndex = entity.lightningCaster1ae;
    if (casterIndex < 0 || static_cast<std::size_t>(casterIndex) >= kEntitySlotCount)
    {
      return;
    }
    OriginalEntity &caster = pool.slot(static_cast<std::size_t>(casterIndex));

    const float want = DAT_00355558_summonFacing(spell);
    const float step = FUN_0023a320_approach_angle(
        caster.facingRadians5c, want, static_cast<float>(environment.frameTicks) * spell.turnRate);
    // FUN_0023A320 answers zero both for "already there" and for "inside the
    // dead zone", and the caller treats both as "snap to the target".
    caster.facingRadians5c = (step == 0.0f) ? want : caster.facingRadians5c + step;

    entity.facingRadians5c = caster.facingRadians5c;
    entity.positionX20 = caster.positionX20;
    entity.positionZ24 = caster.positionZ24;
    entity.positionY28 = caster.positionY28;
    entity.groundHeight4c = caster.positionY28 + heightBias;
    entity.previousGroundHeight50 = caster.positionY28 + heightBias;
  }

  // FUN_00266CE8 followed by the same six lines in all four: pull the sample
  // apart into a length and an angle, add the creature's facing to the angle,
  // put it back together, and offset by the creature's position. `radiusBias`
  // and `yawBias` are the two trims Hammer of Evil and Falcon of Death add.
  orphen::ported::psm2::Vec3 summon_curve_point(const OriginalEntity &entity,
                                                const orphen::ported::psm2::Vec3 &sample,
                                                float radiusBias,
                                                float yawBias,
                                                float heightBias)
  {
    const float radius = std::sqrt(sample.x * sample.x + sample.y * sample.y) + radiusBias;
    const float angle = std::atan2(sample.y, sample.x);
    return orphen::ported::psm2::Vec3{
        radius * std::cos(entity.facingRadians5c + angle + yawBias) + entity.positionX20,
        radius * std::sin(entity.facingRadians5c + angle + yawBias) + entity.positionZ24,
        sample.z + entity.positionY28 + heightBias};
  }

  void summon_drive_camera(const OriginalEntity &entity,
                           const ActorEnvironment &environment,
                           float eyeRadiusBias,
                           float eyeHeightBias,
                           float lookAtYawBias,
                           float lookAtHeightBias)
  {
    SummonStage &stage = DAT_0058bb00_summonStage();
    if (stage.eyeRunning())
    {
      const float t = stage.stepEye(environment.frameTicks);
      const orphen::ported::psm2::Vec3 point = summon_curve_point(
          entity, stage.FUN_00266ce8_sample_eye(t), eyeRadiusBias, 0.0f, eyeHeightBias);
      if (environment.camera != nullptr)
      {
        environment.camera->FUN_00217d40_set_eye(point);
      }
    }
    if (stage.lookAtRunning())
    {
      const float t = stage.stepLookAt(environment.frameTicks);
      const orphen::ported::psm2::Vec3 point = summon_curve_point(
          entity, stage.FUN_00266ce8_sample_look_at(t), 0.0f, lookAtYawBias, lookAtHeightBias);
      if (environment.camera != nullptr)
      {
        environment.camera->FUN_00217d10_set_look_at(point);
      }
    }
  }

  // `(+0xAA & 0xF00) == phase` with +0x06 bit 4 up, and nothing already
  // speaking: the creature's three shouts. The channel is DAT_0031DA65 indexed
  // by the caster's +0x95, the same byte the cast incantation uses.
  bool summon_marker(const OriginalEntity &entity, std::uint16_t phase)
  {
    return (entity.flagsAa & 0xF00u) == phase && (entity.flags06 & 4u) != 0;
  }

  void summon_voice(const ActorEnvironment &environment,
                    std::uint8_t channel,
                    std::uint32_t clipIndex)
  {
    if (!environment.FUN_00206a90_voice_busy || environment.FUN_00206a90_voice_busy())
    {
      return;
    }
    if (environment.FUN_00206f08_play_voice)
    {
      environment.FUN_00206f08_play_voice(channel, clipIndex);
    }
  }

  std::uint8_t summon_voice_channel(const OriginalEntity &entity,
                                    const ActorEnvironment &environment)
  {
    EntityPool &pool = *environment.entityPool;
    const std::int16_t casterIndex = entity.lightningCaster1ae;
    if (casterIndex < 0 || static_cast<std::size_t>(casterIndex) >= kEntitySlotCount ||
        !environment.DAT_0031da65_voiceChannel)
    {
      return 0;
    }
    return environment.DAT_0031da65_voiceChannel(
        static_cast<std::int16_t>(pool.slot(static_cast<std::size_t>(casterIndex)).byte95));
  }

  // DAT_0031DA1E and DAT_0031DA20: the first-time spirit-name banner, armed for
  // sixty frames when DAT_0031DA1C's bit for this spirit is up. **Not ported**:
  // nothing in the executable draws it -- the only reader is FUN_0023FD30's
  // timer countdown -- so there is no display to feed. Left as a named gap
  // rather than an approximation.
  void summon_banner(const SummonSpell &) {}

  // FUN_002E2D38 and its three siblings again, with the summon branch closed:
  // the creature's damage pass is thrown at level **6**, which clamps to 5 for
  // everything that reads it and is not 5 for the `level == 5` test, so the
  // launch cannot hand back to another creature. It is thrown from the target's
  // slot as the caster, so the blast lands on what was aimed at.
  std::int32_t FUN_002e2d38_launch_elemental(const ElementalSpellB &spell,
                                             std::uint8_t element,
                                             std::uint8_t level,
                                             std::uint16_t attackPower,
                                             std::int16_t target,
                                             std::uint32_t hitParameters,
                                             std::int16_t casterSlot,
                                             const orphen::ported::psm2::Vec3 &summonAnchor,
                                             const orphen::ported::psm2::Vec3 &castPosition,
                                             const ActorEnvironment &environment);

  void summon_damage_pass(const OriginalEntity &entity,
                          const ActorEnvironment &environment,
                          const SummonSpell &spell)
  {
    const ElementalSpellB *elemental = elementalSpellBForHand(spell.handType);
    if (elemental == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    const std::int16_t target = entity.lightningTarget1ac;
    // `iVar20 < 3` takes an uninitialised stack block as the cast position in
    // the original -- a real quirk, and unreachable in practice because the
    // launch only reaches a summon for a target above 1 and slot 2 is the one
    // value in that range the branch would catch. The port passes the
    // creature's own anchor there instead of reading uninitialised memory.
    orphen::ported::psm2::Vec3 from{entity.summonAnchorX19c, entity.summonAnchorZ1a0,
                                    entity.summonAnchorY1a4};
    if (target >= 3 && static_cast<std::size_t>(target) < kEntitySlotCount)
    {
      const OriginalEntity &victim = pool.slot(static_cast<std::size_t>(target));
      from = orphen::ported::psm2::Vec3{victim.positionX20, victim.positionZ24, victim.positionY28};
    }
    const orphen::ported::psm2::Vec3 anchor{entity.summonAnchorX19c, entity.summonAnchorZ1a0,
                                            entity.summonAnchorY1a4};
    FUN_002e2d38_launch_elemental(*elemental, 1, 6,
                                  static_cast<std::uint16_t>(entity.attackPower12c), target,
                                  entity.hitParameters198, target, anchor, from, environment);
  }

  // The tail all four share: the creature's own fade past `fadeFromFrame`, then
  // the stage's ramp in and out, and the release when the ramp has run back up.
  // `+0x94` is the whole state machine -- 1 while the stage darkens, 3 while it
  // comes back, and -100 the frame it is done.
  void summon_tail(OriginalEntity &entity,
                   std::size_t slot,
                   const ActorEnvironment &environment,
                   const SummonSpell &spell)
  {
    EntityPool &pool = *environment.entityPool;
    SummonStage &stage = DAT_0058bb00_summonStage();
    const std::int32_t ticks = static_cast<std::int32_t>(environment.frameTicks);
    const std::int8_t state = static_cast<std::int8_t>(entity.spawnParam94);

    if (state == -100)
    {
      SummonStage::FUN_002de500_release_field(pool);
      if (environment.DAT_00355700_globalFadeCap != nullptr)
      {
        *environment.DAT_00355700_globalFadeCap = 0;
      }
      pool.releaseSlot(slot);
      if (environment.DAT_00343878_frameFeedback != nullptr)
      {
        environment.DAT_00343878_frameFeedback->FUN_00264470_set_alpha_and_transform(0, 0, 0, 0, 0,
                                                                                     0);
      }
      if (environment.DAT_00354ecc_setBattleSuspended)
      {
        environment.DAT_00354ecc_setBattleSuspended(0);
      }
      return;
    }

    if (state == 1)
    {
      stage.DAT_0035554c_stageFade() -= ticks * 9;
      if (stage.DAT_0035554c_stageFade() < 300)
      {
        stage.DAT_0035554c_stageFade() = 300;
        entity.spawnParam94 = 0xFF;
      }
      const std::uint8_t cap = static_cast<std::uint8_t>(stage.DAT_0035554c_stageFade() / 100);
      if (environment.DAT_00355700_globalFadeCap != nullptr)
      {
        *environment.DAT_00355700_globalFadeCap = cap;
      }
      stage.DAT_00355550_veilAlpha() = static_cast<std::uint8_t>(0x7F - cap);
    }

    if (static_cast<std::int8_t>(entity.spawnParam94) == 3)
    {
      stage.DAT_0035554c_stageFade() += ticks * spell.exitRate;
      if (stage.DAT_0035554c_stageFade() > 0x319C)
      {
        stage.DAT_0035554c_stageFade() = 0x1E;
        entity.spawnParam94 = 0x9C; // -100
      }
      const std::uint8_t cap = static_cast<std::uint8_t>(stage.DAT_0035554c_stageFade() / 100);
      const std::uint8_t veil = static_cast<std::uint8_t>(0x7F - cap);
      if (environment.DAT_00355700_globalFadeCap != nullptr)
      {
        *environment.DAT_00355700_globalFadeCap = cap;
      }
      stage.DAT_00355550_veilAlpha() = veil;
      // The smear follows the veil down for the last hundred levels, which is
      // what takes the ghost off the screen before the creature does.
      if (veil < 100 && environment.DAT_00343878_frameFeedback != nullptr)
      {
        environment.DAT_00343878_frameFeedback->FUN_00264448_set_alpha(veil);
      }
    }

    // FUN_002D7038(colour, DAT_00355550, 0x4000, 2): the veil. Not ported --
    // see original_summon_stage.h. Its alpha is tracked above so the smear's
    // handoff stays faithful.
    stage.FUN_002d6fa0_apply(
        pool, environment.DAT_00355700_globalFadeCap != nullptr
                  ? *environment.DAT_00355700_globalFadeCap
                  : 0);
    (void)spell;
  }

  // The creature's own fade, which runs on the timeline rather than on the
  // stage: past the frame each spell names, DAT_00355554 counts down the same
  // way and lands in +0x134.
  void summon_creature_fade(OriginalEntity &entity,
                            const ActorEnvironment &environment,
                            const SummonSpell &spell)
  {
    if (static_cast<std::int16_t>(entity.timelineCursorA8) <= spell.fadeFromFrame)
    {
      return;
    }
    SummonStage &stage = DAT_0058bb00_summonStage();
    stage.DAT_00355554_creatureFade() -= static_cast<std::int32_t>(environment.frameTicks) * 9;
    if (stage.DAT_00355554_creatureFade() < 300)
    {
      stage.DAT_00355554_creatureFade() = 300;
    }
    entity.fadeLevel134 = static_cast<std::uint8_t>(stage.DAT_00355554_creatureFade() / 100);
  }

  // ---------------------------------------------------- Pinnacle of the Sun
  //
  // FUN_002E01F8. The one summon that puts the camera somewhere of its own on
  // the release frame instead of leaving it on the curve: the eye goes 1.3
  // units out at 100 degrees off the creature's facing and two up, and the
  // look-at four units straight ahead.
  inline constexpr float kDAT_0035492c_pinnacleEyeYaw = 1.7453299760818481f;
  inline constexpr float kDAT_00354930_pinnacleEyeRadius = 1.2999999523162842f;

  void FUN_002e01f8_pinnacle_summon(OriginalEntity &entity,
                                    std::size_t slot,
                                    const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    const SummonSpell &spell = kSummonSpells[2];
    const std::int8_t state = static_cast<std::int8_t>(entity.spawnParam94);

    entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 0x80u);
    if (state == 0)
    {
      entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 1u);
      summon_enter(entity, slot, environment, true);
      entity.spawnParam94 = 100;
      return;
    }
    if (state == 100)
    {
      summon_begin_camera(entity, environment, spell);
    }
    summon_track_caster(entity, environment, spell, 0.0f);
    summon_drive_camera(entity, environment, 0.0f, 0.0f, 0.0f, 0.0f);

    const std::uint8_t channel = summon_voice_channel(entity, environment);
    if (summon_marker(entity, 0x700))
    {
      summon_voice(environment, channel, 2);
    }
    if (static_cast<std::int16_t>(entity.timelineCursorA8) == spell.bannerFrame &&
        (entity.flags06 & 4u) != 0)
    {
      summon_voice(environment, channel, 5);
      summon_banner(spell);
    }
    if (summon_marker(entity, 0x600))
    {
      summon_voice(environment, channel, 3);
    }
    if (summon_marker(entity, 0x300))
    {
      summon_voice(environment, channel, 4);
      if (environment.camera != nullptr)
      {
        environment.camera->FUN_00217d40_set_eye(orphen::ported::psm2::Vec3{
            std::cos(entity.facingRadians5c + kDAT_0035492c_pinnacleEyeYaw) *
                    kDAT_00354930_pinnacleEyeRadius +
                entity.positionX20,
            std::sin(entity.facingRadians5c + kDAT_0035492c_pinnacleEyeYaw) *
                    kDAT_00354930_pinnacleEyeRadius +
                entity.positionZ24,
            entity.positionY28 + 2.0f});
        environment.camera->FUN_00217d10_set_look_at(orphen::ported::psm2::Vec3{
            std::cos(entity.facingRadians5c) * 4.0f + entity.positionX20,
            std::sin(entity.facingRadians5c) * 4.0f + entity.positionZ24, entity.positionY28});
      }
      summon_damage_pass(entity, environment, spell);
      // DAT_00354EC0 is the "a battle has already ended" latch; the original
      // skips the release when it is up, because the field is being torn down
      // anyway.
      if (environment.DAT_00354ec0_markerTable == 0)
      {
        SummonStage::FUN_002de548_release_hurt(pool);
      }
    }

    summon_creature_fade(entity, environment, spell);
    if (static_cast<std::int16_t>(entity.timelineCursorA8) == spell.endFrame &&
        (entity.flags06 & 4u) != 0)
    {
      entity.spawnParam94 = 3;
    }
    if (environment.DAT_00343878_frameFeedback != nullptr)
    {
      environment.DAT_00343878_frameFeedback->FUN_00264448_set_alpha(0x5A);
      environment.DAT_00343878_frameFeedback->set_DAT_0034387c_scale(0x28, 0x28);
    }
    // Pinnacle's exit test is its own: +0x06 bit 0 up ends the run whatever the
    // state byte says, where the other three only end on -100.
    if ((entity.flags06 & 1u) != 0)
    {
      entity.spawnParam94 = 0x9C;
    }
    summon_tail(entity, slot, environment, spell);
  }

  // ------------------------------------------------------ Hail of Heavens
  //
  // FUN_002E1320. Its release frame drops the camera 1.5 units out at
  // DAT_00354938 off the facing and points it at the *target*, not ahead of the
  // creature, and it hands the caster back before the damage rather than after.
  // It is also the only one of the four that never turns the smear on.
  inline constexpr float kDAT_00354938_hailEyeYaw = 2.9670600891113281f;
  inline constexpr float kDAT_0035493c_hailEyeHeight = 1.3999999761581421f;

  void FUN_002e1320_hail_summon(OriginalEntity &entity,
                                std::size_t slot,
                                const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    const SummonSpell &spell = kSummonSpells[3];
    const std::int8_t state = static_cast<std::int8_t>(entity.spawnParam94);

    entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 0x80u);
    if (state == 0)
    {
      entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 1u);
      summon_enter(entity, slot, environment, true);
      // Hail alone holds at the entry frame until the voice is clear, redoing
      // the whole setup each time. Everything it does is a write, so repeating
      // it is idempotent.
      if (environment.FUN_00206a90_voice_busy && environment.FUN_00206a90_voice_busy())
      {
        return;
      }
      entity.spawnParam94 = 100;
      return;
    }
    if (state == 100)
    {
      summon_begin_camera(entity, environment, spell);
    }
    summon_track_caster(entity, environment, spell, 0.0f);
    summon_drive_camera(entity, environment, 0.0f, 0.0f, 0.0f, 0.0f);

    const std::uint8_t channel = summon_voice_channel(entity, environment);
    if ((entity.flags06 & 4u) != 0)
    {
      if ((entity.flagsAa & 0xF00u) == 0x700)
      {
        summon_voice(environment, channel, 2);
      }
      if ((entity.flags06 & 4u) != 0 && (entity.flagsAa & 0xF00u) == 0x600)
      {
        summon_voice(environment, channel, 3);
      }
      if ((entity.flags06 & 4u) != 0 && (entity.flagsAa & 0xF00u) == 0x300)
      {
        summon_voice(environment, channel, 4);
        const std::int16_t target = entity.lightningTarget1ac;
        if (environment.camera != nullptr)
        {
          environment.camera->FUN_00217d40_set_eye(orphen::ported::psm2::Vec3{
              std::cos(entity.facingRadians5c + kDAT_00354938_hailEyeYaw) * 1.5f +
                  entity.positionX20,
              std::sin(entity.facingRadians5c + kDAT_00354938_hailEyeYaw) * 1.5f +
                  entity.positionZ24,
              entity.positionY28 + kDAT_0035493c_hailEyeHeight});
          if (target >= 0 && static_cast<std::size_t>(target) < kEntitySlotCount)
          {
            const OriginalEntity &victim = pool.slot(static_cast<std::size_t>(target));
            // The look-at adds the *target's* position to the creature's for x
            // and z and then takes the target's height on its own -- the
            // original's own asymmetry, not a transcription slip.
            environment.camera->FUN_00217d10_set_look_at(orphen::ported::psm2::Vec3{
                victim.positionX20 + entity.positionX20, victim.positionZ24 + entity.positionZ24,
                victim.positionY28});
          }
        }
        const std::int16_t casterIndex = entity.lightningCaster1ae;
        if (casterIndex >= 0 && static_cast<std::size_t>(casterIndex) < kEntitySlotCount)
        {
          SummonStage::FUN_002de640_release_one(pool.slot(static_cast<std::size_t>(casterIndex)));
        }
        summon_damage_pass(entity, environment, spell);
        SummonStage::FUN_002de548_release_hurt(pool);
      }
    }

    summon_creature_fade(entity, environment, spell);
    if (static_cast<std::int16_t>(entity.timelineCursorA8) == spell.endFrame &&
        (entity.flags06 & 4u) != 0)
    {
      entity.spawnParam94 = 3;
    }
    if ((entity.flags06 & 1u) != 0)
    {
      entity.spawnParam94 = 0x9C;
    }
    const bool ending = static_cast<std::int8_t>(entity.spawnParam94) == -100;
    summon_tail(entity, slot, environment, spell);
    if (ending && environment.DAT_00343878_frameFeedback != nullptr)
    {
      environment.DAT_00343878_frameFeedback->set_DAT_0034387c_scale(0, 0);
    }
    if (ending)
    {
      summon_banner(spell);
    }
  }

  // -------------------------------------------------------- Hammer of Evil
  //
  // FUN_002E23E8. The shortest curve of the four -- four eye points and one
  // look-at, over 0x1D88 rather than 0x21C0 -- with a constant trim on each:
  // the eye is pushed out by DAT_00354948 and lifted half a unit, and the
  // look-at is swung DAT_0035494C round and lifted DAT_00354950. It also
  // releases the whole field before its damage rather than only the victims,
  // and its end marker is +0x06 bit **3**, not bit 2.
  inline constexpr float kDAT_00354948_hammerEyeRadius = 0.2000000029802322f;
  inline constexpr float kDAT_0035494c_hammerLookAtYaw = 0.3490660190582275f;
  inline constexpr float kDAT_00354950_hammerLookAtHeight = 0.2000000029802322f;

  void FUN_002e23e8_hammer_summon(OriginalEntity &entity,
                                  std::size_t slot,
                                  const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    const SummonSpell &spell = kSummonSpells[1];
    const std::int8_t state = static_cast<std::int8_t>(entity.spawnParam94);

    entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 0x80u);
    if (state == 0)
    {
      entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 1u);
      summon_enter(entity, slot, environment, true);
      entity.spawnParam94 = 100;
      return;
    }
    if (state == 100)
    {
      summon_begin_camera(entity, environment, spell);
    }
    summon_track_caster(entity, environment, spell, 0.0f);
    summon_drive_camera(entity, environment, kDAT_00354948_hammerEyeRadius, 0.5f,
                        kDAT_0035494c_hammerLookAtYaw, kDAT_00354950_hammerLookAtHeight);

    const std::uint8_t channel = summon_voice_channel(entity, environment);
    if (summon_marker(entity, 0x700))
    {
      summon_voice(environment, channel, 2);
    }
    if (summon_marker(entity, 0x600))
    {
      summon_voice(environment, channel, 3);
    }
    if (static_cast<std::int16_t>(entity.timelineCursorA8) == spell.bannerFrame &&
        (entity.flags06 & 4u) != 0)
    {
      summon_banner(spell);
    }
    if (summon_marker(entity, 0x300))
    {
      summon_voice(environment, channel, 4);
      // Hammer lifts the freeze off everything before the blast, not just off
      // the victims, and never puts it back -- the field is running again for
      // the whole of its exit.
      SummonStage::FUN_002de500_release_field(pool);
      summon_damage_pass(entity, environment, spell);
      SummonStage::FUN_002de548_release_hurt(pool);
    }

    summon_creature_fade(entity, environment, spell);
    if (static_cast<std::int16_t>(entity.timelineCursorA8) == spell.endFrame &&
        (entity.flags06 & 8u) != 0 && static_cast<std::int8_t>(entity.spawnParam94) != -100)
    {
      entity.spawnParam94 = 3;
    }
    if ((entity.flags06 & 1u) != 0)
    {
      entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10u);
    }
    if (environment.DAT_00343878_frameFeedback != nullptr)
    {
      environment.DAT_00343878_frameFeedback->FUN_00264448_set_alpha(0x5A);
      environment.DAT_00343878_frameFeedback->set_DAT_0034387c_scale(0x28, 0x28);
    }
    const bool ending = static_cast<std::int8_t>(entity.spawnParam94) == -100;
    summon_tail(entity, slot, environment, spell);
    if (ending && environment.DAT_00343878_frameFeedback != nullptr)
    {
      environment.DAT_00343878_frameFeedback->set_DAT_0034387c_scale(0, 0);
    }
  }

  // ------------------------------------------------------ Falcon of Death
  //
  // FUN_002E34B8. The longest curve -- nine eye points -- and the only one that
  // trims the whole stage rather than the camera: half a unit on the creature's
  // ground height and on both curve samples, DAT_00343880 flipped so the smear
  // mirrors every frame, +0x133 pushed to 0xD0 so the creature draws in front
  // of the field, and DAT_0058BFE3 -- slot 0's +0x133 -- to 0x30 so the player
  // draws behind it. Its exit ramp is twice everyone else's.
  void FUN_002e34b8_falcon_summon(OriginalEntity &entity,
                                  std::size_t slot,
                                  const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    const SummonSpell &spell = kSummonSpells[0];
    const std::int8_t state = static_cast<std::int8_t>(entity.spawnParam94);

    entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 0x80u);
    if (state == 0)
    {
      entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 1u);
      // DAT_00355E14 = 0. Falcon is the only summon that touches it, and
      // nothing else in the executable reads it back.
      summon_enter(entity, slot, environment, false);
      entity.spawnParam94 = 100;
      return;
    }
    if (state == 100)
    {
      summon_begin_camera(entity, environment, spell);
    }
    summon_track_caster(entity, environment, spell, 0.5f);
    summon_drive_camera(entity, environment, 0.5f, 0.5f, 0.0f, 0.5f);

    if (environment.DAT_00343878_frameFeedback != nullptr)
    {
      environment.DAT_00343878_frameFeedback->FUN_00264448_set_alpha(0x5A);
      environment.DAT_00343878_frameFeedback->flip_DAT_00343880_rotation();
      environment.DAT_00343878_frameFeedback->set_DAT_0034387c_scale(0x28, 0x28);
    }
    entity.depthBias133 = static_cast<std::int8_t>(0xD0);
    pool.slot(0).depthBias133 = 0x30;

    const std::uint8_t channel = summon_voice_channel(entity, environment);
    if (summon_marker(entity, 0x700))
    {
      summon_voice(environment, channel, 2);
    }
    if (summon_marker(entity, 0x600))
    {
      summon_voice(environment, channel, 3);
    }
    if (static_cast<std::int16_t>(entity.timelineCursorA8) == spell.bannerFrame &&
        (entity.flags06 & 4u) != 0)
    {
      summon_banner(spell);
    }
    if (summon_marker(entity, 0x300))
    {
      summon_voice(environment, channel, 4);
      summon_damage_pass(entity, environment, spell);
      SummonStage::FUN_002de548_release_hurt(pool);
    }

    summon_creature_fade(entity, environment, spell);
    if (static_cast<std::int16_t>(entity.timelineCursorA8) == spell.endFrame &&
        (entity.flags06 & 4u) != 0 && static_cast<std::int8_t>(entity.spawnParam94) != -100)
    {
      entity.spawnParam94 = 3;
    }
    if ((entity.flags06 & 1u) != 0)
    {
      entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10u);
    }
    const bool ending = static_cast<std::int8_t>(entity.spawnParam94) == -100;
    summon_tail(entity, slot, environment, spell);
    if (ending && environment.DAT_00343878_frameFeedback != nullptr)
    {
      environment.DAT_00343878_frameFeedback->set_DAT_0034387c_scale(0, 0);
    }
  }

  void FUN_002e01f8_summon(OriginalEntity &entity,
                           std::size_t slot,
                           const ActorEnvironment &environment)
  {
    switch (entity.typeId00)
    {
    case 0x13F:
      FUN_002e34b8_falcon_summon(entity, slot, environment);
      break;
    case 0x140:
      FUN_002e23e8_hammer_summon(entity, slot, environment);
      break;
    case 0x141:
      FUN_002e01f8_pinnacle_summon(entity, slot, environment);
      break;
    case 0x142:
      FUN_002e1320_hail_summon(entity, slot, environment);
      break;
    default:
      break;
    }
  }

  // FUN_002E00D8 / FUN_002E0E60 / FUN_002E1F28 / FUN_002E2F50: the spawners.
  // One shape for all four -- Falcon of Death alone follows it with a second
  // entity, a type 0x1D9 flash parked at the caster's feet.
  std::int32_t FUN_002e00d8_spawn_summon(const SummonSpell &spell,
                                         std::uint8_t element,
                                         std::uint16_t attackPower,
                                         std::int16_t target,
                                         std::uint32_t hitParameters,
                                         const orphen::ported::psm2::Vec3 &anchor,
                                         std::int16_t casterSlot,
                                         const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr || environment.descriptors == nullptr)
    {
      return -1;
    }
    EntityPool &pool = *environment.entityPool;
    const std::size_t slot = pool.FUN_00265e28_allocate_and_initialize(
        static_cast<std::uint32_t>(spell.summonType), *environment.descriptors);
    if (slot >= kEntitySlotCount)
    {
      return -1;
    }
    const bool haveCaster =
        casterSlot >= 0 && static_cast<std::size_t>(casterSlot) < kEntitySlotCount;
    const float casterFacing =
        haveCaster ? pool.slot(static_cast<std::size_t>(casterSlot)).facingRadians5c : 0.0f;

    OriginalEntity &creature = pool.slot(slot);
    creature.spawnParam94 = 0;
    creature.summonAnchorX19c = anchor.x;
    creature.positionX20 = anchor.x;
    creature.attackPower12c = attackPower;
    creature.summonAnchorZ1a0 = anchor.y;
    creature.positionZ24 = anchor.y;
    creature.halfword04 = 0x19;
    creature.descriptorFlags02 =
        static_cast<std::uint16_t>(creature.descriptorFlags02 | 0x1000u);
    // Falcon of Death lifts its anchor half a unit and stores the lifted value;
    // the other three keep it as it came.
    const float height =
        (spell.summonType == 0x13F) ? anchor.z + 0.5f : anchor.z;
    creature.summonAnchorY1a4 = height;
    creature.groundHeight4c = height;
    creature.positionY28 = height;
    creature.previousGroundHeight50 = height;
    creature.facingRadians5c = casterFacing;
    creature.hitParameters198 = hitParameters;
    creature.lightningTarget1ac = target;
    creature.lightningCaster1ae = casterSlot;
    creature.lightningLevel1b3 = 5;
    creature.fadeRamp62 = 0;
    creature.animationA0 = spell.spawnAnimation;
    creature.lightningTimer1b0 = FUN_00248e48_arm_timer(0x20);
    (void)element;

    if (spell.summonType == 0x13F)
    {
      // The flash. It is allocated as **0x175** -- Falcon's hand -- and then
      // retyped to 0x1D9, so it draws the hand's model with the flash's
      // behaviour, the same trick the elemental launches use for their discs.
      const std::size_t flashSlot =
          pool.FUN_00265e28_allocate_and_initialize(0x175, *environment.descriptors);
      if (flashSlot < kEntitySlotCount && haveCaster)
      {
        const OriginalEntity &caster = pool.slot(static_cast<std::size_t>(casterSlot));
        OriginalEntity &flash = pool.slot(flashSlot);
        flash.typeId00 = 0x1D9;
        flash.positionX20 = caster.positionX20;
        flash.positionZ24 = caster.positionZ24;
        flash.animationA0 = 4;
        flash.halfword08 = static_cast<std::uint16_t>(flash.halfword08 | 0x80u);
        flash.halfword04 = 0x19;
        const float lifted = caster.positionY28 + kFGpffffa9e8_flashLift;
        flash.groundHeight4c = lifted;
        flash.positionY28 = lifted;
        flash.previousGroundHeight50 = lifted;
        flash.facingRadians5c = caster.facingRadians5c;
      }
    }
    return static_cast<std::int32_t>(slot);
  }


  // ---------------------------------------------- Bite of Lightning's summon
  //
  // FUN_002DF018, type 0x13E, spawned by FUN_002DEEF0. It sets the same stage
  // as the other four -- the same freeze, the same dim set, the same two fade
  // ramps out of the same two globals -- and then does three things none of
  // them do:
  //
  //  * **Its camera is its own rig, not a curve.** FUN_0020DD78 finds the bone
  //    carrying role 1 on the creature's model and role 2 beside it, and the
  //    eye and the look-at are simply those two bones' world positions. There
  //    are no spline points anywhere for it.
  //  * **Its state machine runs off the animation's own phase word** rather
  //    than off timeline frames: 0x400 ends the creature, 0x500 starts its
  //    fade, and the release is the frame +0x08 bit 0 comes up, not a frame
  //    number.
  //  * **Its fields are all somewhere else.** FUN_002DEEF0 writes the target to
  //    +0x1A8, the caster to +0x1AA, the timer to +0x1AC and the level to
  //    +0x1B4 -- every one of them different from the block the other four
  //    share -- and it does its own freeze at spawn rather than waiting for the
  //    creature's first frame.
  //
  // fGpffffb5D8 is a fifth facing global beside the other four, and
  // iGpffffb5D4 / iGpffffb5CC / cGpffffb790 / cGpffffb5D0 are the same
  // DAT_00355554 / DAT_0035554C / DAT_00355700 / DAT_00355550 the rest use.
  inline constexpr float kFGpffffa9b0_biteTurnRate = 0.0026179900858551f;
  inline constexpr float kFGpffffa9b4_biteLift = 0.1000000014901161f;

  float &fGpffffb5d8_biteFacing()
  {
    static float facing = 0.0f;
    return facing;
  }

  std::int32_t FUN_002de650_launch_lightning(std::uint8_t level,
                                             std::uint16_t attackPower,
                                             std::int16_t target,
                                             std::uint32_t hitParameters,
                                             std::int16_t casterSlot,
                                             const orphen::ported::psm2::Vec3 &summonAnchor,
                                             const orphen::ported::psm2::Vec3 &castPosition,
                                             const ActorEnvironment &environment);

  // FUN_002DEEF0: the spawner. Unlike the other four it freezes the field
  // itself, on the frame the creature is made, and hands the bit straight back
  // to the creature and to the player.
  std::int32_t FUN_002deef0_spawn_bite_summon(std::uint16_t attackPower,
                                              std::int16_t target,
                                              std::uint32_t hitParameters,
                                              const orphen::ported::psm2::Vec3 &anchor,
                                              std::int16_t casterSlot,
                                              std::uint8_t level,
                                              const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr || environment.descriptors == nullptr)
    {
      return -1;
    }
    EntityPool &pool = *environment.entityPool;
    const std::size_t slot =
        pool.FUN_00265e28_allocate_and_initialize(0x13E, *environment.descriptors);
    if (slot >= kEntitySlotCount)
    {
      return -1;
    }
    OriginalEntity &creature = pool.slot(slot);
    creature.spawnParam94 = 0;
    creature.summonAnchorX19c = anchor.x;
    creature.attackPower12c = attackPower;
    creature.positionX20 = anchor.x;
    creature.descriptorFlags02 =
        static_cast<std::uint16_t>(creature.descriptorFlags02 | 0x1000u);
    creature.halfword04 = 0x19;
    creature.summonAnchorZ1a0 = anchor.y;
    creature.positionZ24 = anchor.y;
    creature.summonAnchorY1a4 = anchor.z;
    creature.groundHeight4c = anchor.z;
    creature.positionY28 = anchor.z;
    creature.previousGroundHeight50 = anchor.z;
    creature.hitParameters198 = hitParameters;
    creature.biteSummonTarget1a8 = target;
    creature.biteSummonCaster1aa = casterSlot;
    creature.biteSummonLevel1b4 = static_cast<std::int8_t>(level);
    creature.fadeRamp62 = 0;
    creature.animationA0 = 5;
    creature.biteSummonTimer1ac = static_cast<std::uint16_t>(FUN_00248e48_arm_timer(0x20));

    if (environment.DAT_00354ecc_setBattleSuspended)
    {
      environment.DAT_00354ecc_setBattleSuspended(1);
    }
    SummonStage::FUN_002de4a8_freeze_field(pool);
    SummonStage::FUN_002de640_release_one(creature);
    SummonStage::FUN_002de640_release_one(pool, 0);
    return static_cast<std::int32_t>(slot);
  }

  void FUN_002df018_bite_summon(OriginalEntity &entity,
                                std::size_t slot,
                                const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    SummonStage &stage = DAT_0058bb00_summonStage();
    const std::int32_t ticks = static_cast<std::int32_t>(environment.frameTicks);
    const std::int16_t casterIndex = entity.biteSummonCaster1aa;
    const bool haveCaster =
        casterIndex >= 0 && static_cast<std::size_t>(casterIndex) < kEntitySlotCount;

    entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 0x80u);

    const std::uint8_t channel =
        (haveCaster && environment.DAT_0031da65_voiceChannel)
            ? environment.DAT_0031da65_voiceChannel(static_cast<std::int16_t>(
                  pool.slot(static_cast<std::size_t>(casterIndex)).byte95))
            : 0;

    if (static_cast<std::int8_t>(entity.spawnParam94) == 0)
    {
      const std::int16_t target = entity.biteSummonTarget1a8;
      if (haveCaster && target >= 0 && static_cast<std::size_t>(target) < kEntitySlotCount)
      {
        const OriginalEntity &caster = pool.slot(static_cast<std::size_t>(casterIndex));
        const OriginalEntity &victim = pool.slot(static_cast<std::size_t>(target));
        fGpffffb5d8_biteFacing() = std::atan2(victim.positionZ24 - caster.positionZ24,
                                              victim.positionX20 - caster.positionX20);
      }
      entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10u);
      entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 1u);
      if (environment.DAT_00354ecc_setBattleSuspended)
      {
        environment.DAT_00354ecc_setBattleSuspended(1);
      }

      const ActorEnvironment::SummonExemptSlots exempt =
          haveCaster ? summon_exempt(pool.slot(static_cast<std::size_t>(casterIndex)), environment)
                     : ActorEnvironment::SummonExemptSlots{};
      stage.FUN_002d6e20_build_dim_set(pool);
      stage.FUN_002d6f38_exclude(static_cast<std::int32_t>(slot));
      stage.FUN_002d6f38_exclude(casterIndex);
      stage.FUN_002d6f38_exclude(exempt.DAT_0031daac_shield);
      entity.spawnParam94 = 0x5A;
      SummonStage::FUN_002de640_release_one(pool, exempt.DAT_0031daac_shield);
      // The caster's shield effect is driven to animation 2 -- its closed pose
      // -- where the other four leave it alone and close the ground ring
      // instead.
      if (exempt.DAT_0031daac_shield >= 0 &&
          static_cast<std::size_t>(exempt.DAT_0031daac_shield) < kEntitySlotCount)
      {
        FUN_00225bc8_set_animation(
            pool.slot(static_cast<std::size_t>(exempt.DAT_0031daac_shield)), 2);
      }
      SummonStage::FUN_002de640_release_one(pool, exempt.DAT_0031da8c_castRing);
      if (exempt.DAT_0031da8c_castRing >= 0 &&
          static_cast<std::size_t>(exempt.DAT_0031da8c_castRing) < kEntitySlotCount)
      {
        pool.slot(static_cast<std::size_t>(exempt.DAT_0031da8c_castRing)).animationA0 = 3;
      }
      stage.FUN_002d6f38_exclude(exempt.DAT_0031da8c_castRing);
      SummonStage::FUN_002de640_release_one(pool, exempt.DAT_0031dad0_sharedHitEffect);
      return;
    }

    // `(byte)(+0x94 + 0xBA) < 0x1E` -- the window 0x46..0x63, i.e. the 0x5A the
    // entry parks on. One frame of nothing, then the camera takes over.
    const std::uint8_t held = static_cast<std::uint8_t>(entity.spawnParam94 + 0xBAu);
    if (held < 0x1E)
    {
      entity.spawnParam94 = 100;
      return;
    }

    if (static_cast<std::int8_t>(entity.spawnParam94) == 100)
    {
      SummonStage::FUN_002de640_release_one(pool, casterIndex);
      entity.fadeRamp62 = 0;
      entity.flags06 = static_cast<std::uint16_t>(entity.flags06 & 0xFFEFu);
      entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 & 0xFFFEu);
      if (environment.DAT_00355700_globalFadeCap != nullptr)
      {
        *environment.DAT_00355700_globalFadeCap = 0x7F;
      }
      entity.spawnParam94 = 1;
      stage.DAT_00355554_creatureFade() = 0x319C;
      stage.DAT_0035554c_stageFade() = 0x319C;
      if (environment.FUN_00267d38_playSound)
      {
        environment.FUN_00267d38_playSound(0xEC, entity);
      }
      if (environment.DAT_00343878_frameFeedback != nullptr)
      {
        environment.DAT_00343878_frameFeedback->set_DAT_0034387c_scale(1, 1);
        environment.DAT_00343878_frameFeedback->FUN_00264448_set_alpha(100);
      }
    }

    if (haveCaster)
    {
      OriginalEntity &caster = pool.slot(static_cast<std::size_t>(casterIndex));
      const float want = fGpffffb5d8_biteFacing();
      const float step = FUN_0023a320_approach_angle(
          caster.facingRadians5c, want,
          static_cast<float>(environment.frameTicks) * kFGpffffa9b0_biteTurnRate);
      caster.facingRadians5c = (step == 0.0f) ? want : caster.facingRadians5c + step;
      entity.facingRadians5c = caster.facingRadians5c;
      entity.positionX20 = caster.positionX20;
      entity.positionZ24 = caster.positionZ24;
      const float lifted = caster.positionY28 + kFGpffffa9b4_biteLift;
      entity.groundHeight4c = lifted;
      entity.positionY28 = lifted;
      entity.previousGroundHeight50 = lifted;
    }

    // The camera rig: roles 1 and 2 on the creature's own model.
    if (environment.camera != nullptr && environment.FUN_0020dd78_bone_for_role &&
        environment.FUN_0020dc88_bone_point)
    {
      const orphen::ported::psm2::Vec3 origin{0.0f, 0.0f, 0.0f};
      const std::size_t eyeBone = environment.FUN_0020dd78_bone_for_role(slot, 1);
      environment.camera->FUN_00217d40_set_eye(
          environment.FUN_0020dc88_bone_point(slot, eyeBone, origin));
      const std::size_t lookBone = environment.FUN_0020dd78_bone_for_role(slot, 2);
      environment.camera->FUN_00217d10_set_look_at(
          environment.FUN_0020dc88_bone_point(slot, lookBone, origin));
      environment.camera->setZoomLog2(1.0f);
      environment.camera->setRoll(0.0f);
    }

    if (summon_marker(entity, 0x700))
    {
      summon_voice(environment, channel, 2);
    }
    // The second shout keys on +0x06 bit **3**, not bit 2 like the first.
    if ((entity.flags06 & 8u) != 0 && (entity.flagsAa & 0xF00u) == 0x600)
    {
      summon_voice(environment, channel, 3);
    }
    if ((entity.flagsAa & 0xF00u) == 0x200 && (entity.flags06 & 4u) != 0)
    {
      // DAT_0031DA1E = 1: the spirit-name banner. Not ported -- nothing draws
      // it; see the four above.
    }
    if (summon_marker(entity, 0x300))
    {
      summon_voice(environment, channel, 4);
      // The blast is a level-(n-1) Bite cast *from whatever the caster is
      // aimed at*, which is why it cannot recurse into another creature.
      orphen::ported::psm2::Vec3 landing{entity.positionX20, entity.positionZ24,
                                         entity.positionY28};
      std::int32_t victim = -1;
      if (haveCaster && environment.FUN_002493f0_spell_landing)
      {
        victim = environment.FUN_002493f0_spell_landing(static_cast<std::size_t>(casterIndex),
                                                        landing);
      }
      orphen::ported::psm2::Vec3 from = landing;
      if (victim >= 3 && static_cast<std::size_t>(victim) < kEntitySlotCount)
      {
        const OriginalEntity &at = pool.slot(static_cast<std::size_t>(victim));
        from = orphen::ported::psm2::Vec3{at.positionX20, at.positionZ24, at.positionY28};
      }
      const orphen::ported::psm2::Vec3 anchor{entity.summonAnchorX19c, entity.summonAnchorZ1a0,
                                              entity.summonAnchorY1a4};
      FUN_002de650_launch_lightning(static_cast<std::uint8_t>(entity.biteSummonLevel1b4 - 1),
                                    entity.attackPower12c,
                                    static_cast<std::int16_t>(entity.biteSummonTarget1a8),
                                    entity.hitParameters198, static_cast<std::int16_t>(victim),
                                    anchor, from, environment);
      SummonStage::FUN_002de548_release_hurt(pool);
    }
    if ((entity.flagsAa & 0xF00u) == 0x400 && (entity.flags06 & 4u) != 0)
    {
      entity.spawnParam94 = 3;
    }
    if ((entity.flagsAa & 0xF00u) == 0x500)
    {
      entity.fadeRamp62 = 1;
    }
    if (entity.fadeRamp62 != 0)
    {
      stage.DAT_00355554_creatureFade() -= ticks * 9;
      if (stage.DAT_00355554_creatureFade() < 300)
      {
        stage.DAT_00355554_creatureFade() = 300;
        entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 1u);
      }
      entity.fadeLevel134 = static_cast<std::uint8_t>(stage.DAT_00355554_creatureFade() / 100);
    }

    if (static_cast<std::int8_t>(entity.spawnParam94) == 1)
    {
      stage.DAT_0035554c_stageFade() -= ticks * 9;
      if (stage.DAT_0035554c_stageFade() < 300)
      {
        stage.DAT_0035554c_stageFade() = 300;
        entity.spawnParam94 = 0xFF;
      }
      const std::uint8_t cap = static_cast<std::uint8_t>(stage.DAT_0035554c_stageFade() / 100);
      if (environment.DAT_00355700_globalFadeCap != nullptr)
      {
        *environment.DAT_00355700_globalFadeCap = cap;
      }
      stage.DAT_00355550_veilAlpha() = static_cast<std::uint8_t>(0x7F - cap);
    }
    // State 2 is written by nothing in the executable -- every path that could
    // reach it writes 3 instead -- but the original tests for it, so it is kept
    // rather than folded away.
    if (static_cast<std::int8_t>(entity.spawnParam94) == 2)
    {
      stage.DAT_0035554c_stageFade() += ticks * 0x1B;
      if (stage.DAT_0035554c_stageFade() > 0x319C)
      {
        stage.DAT_0035554c_stageFade() = 0;
        entity.spawnParam94 = 0xFF;
      }
    }
    if (static_cast<std::int8_t>(entity.spawnParam94) == 3)
    {
      stage.DAT_0035554c_stageFade() += ticks * 0x1B;
      if (stage.DAT_0035554c_stageFade() > 0x319C)
      {
        stage.DAT_0035554c_stageFade() = 0x1E;
        entity.spawnParam94 = 0xFF;
      }
      const std::uint8_t cap = static_cast<std::uint8_t>(stage.DAT_0035554c_stageFade() / 100);
      std::uint8_t veil = static_cast<std::uint8_t>(0x7F - cap);
      if (environment.DAT_00355700_globalFadeCap != nullptr)
      {
        *environment.DAT_00355700_globalFadeCap = cap;
      }
      // A fully dark stage reads as 1 rather than 0x7F here, which keeps the
      // veil from snapping back to full on the last frame of the ramp.
      if (veil == 0x7F)
      {
        veil = 1;
      }
      stage.DAT_00355550_veilAlpha() = veil;
    }
    // FUN_002D7038(0xA18, DAT_00355550, 0x4000, 2): the veil. Not ported.

    // The release is the frame the animation reports done, not a frame number.
    if (static_cast<std::int16_t>(entity.timelineCursorA8) > 4 && (entity.halfword08 & 1u) != 0)
    {
      if (environment.DAT_00355700_globalFadeCap != nullptr)
      {
        *environment.DAT_00355700_globalFadeCap = 0;
      }
      pool.releaseSlot(slot);
      if (environment.DAT_00343878_frameFeedback != nullptr)
      {
        environment.DAT_00343878_frameFeedback->FUN_00264448_set_alpha(0);
        environment.DAT_00343878_frameFeedback->set_DAT_0034387c_scale(0, 0);
      }
      if (environment.DAT_00354ecc_setBattleSuspended)
      {
        environment.DAT_00354ecc_setBattleSuspended(0);
      }
      SummonStage::FUN_002de500_release_field(pool);
      // FUN_002D6FA0 still runs after the release in the original, on a slot
      // that is already free; the mask's own live test drops it.
    }
    stage.FUN_002d6fa0_apply(pool, environment.DAT_00355700_globalFadeCap != nullptr
                                       ? *environment.DAT_00355700_globalFadeCap
                                       : 0);
  }

  // FUN_002e2d38 / FUN_002e1d20 / FUN_002dfb40 / FUN_002e0c68: the launch.
  //
  // **The level-5 summon is the first branch and it is still a gap**, exactly
  // as it is for Bite of Lightning. FUN_002e2f50 and its three siblings spawn
  // the type 0x13F..0x142 creature whose own behaviour -- three hundred lines
  // apiece -- is not ported; spawning one with nothing to drive it would leave
  // it standing in the arena. The demo in s14_e031 *does* reach this branch
  // (the pinnacle_of_the_sun save state has a type 0x141 on animation 4), so
  // this is the next thing to land, not a branch that cannot happen.
  std::int32_t FUN_002e2d38_launch_elemental(const ElementalSpellB &spell,
                                             std::uint8_t element,
                                             std::uint8_t level,
                                             std::uint16_t attackPower,
                                             std::int16_t target,
                                             std::uint32_t hitParameters,
                                             std::int16_t casterSlot,
                                             const orphen::ported::psm2::Vec3 &summonAnchor,
                                             const orphen::ported::psm2::Vec3 &castPosition,
                                             const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr || environment.descriptors == nullptr)
    {
      return 0;
    }
    EntityPool &pool = *environment.entityPool;

    // FUN_002E2F50 / FUN_002E1F28 / FUN_002E00D8 / FUN_002E0E60: at full charge
    // with a live target the spell is not thrown at all -- it becomes a
    // creature, anchored at the caster's hand rather than at the landing point,
    // and the launch returns nothing for the hand to hold on to.
    if (level == 5 && target > 1)
    {
      const SummonSpell *summon = summonForHand(spell.handType);
      if (summon != nullptr)
      {
        FUN_002e00d8_spawn_summon(*summon, element, attackPower, target, hitParameters,
                                  summonAnchor, casterSlot, environment);
      }
      return 0;
    }

    std::int32_t scaled = (level == 0) ? 1 : static_cast<std::int32_t>(level);
    if (scaled > 5)
    {
      scaled = 5;
    }
    const std::int8_t levelB = static_cast<std::int8_t>(scaled);

    const std::size_t slot =
        pool.FUN_00265e28_allocate_and_initialize(spell.handType, *environment.descriptors);
    if (slot >= kEntitySlotCount)
    {
      return 0;
    }
    auto &projectile = pool.slot(slot);
    projectile.typeId00 = spell.projectileType;
    projectile.modelTypeId15c = spell.handType;
    projectile.attackPower12c =
        spell.foldLevelIntoPower
            ? static_cast<std::uint16_t>(attackPower + scaled * 4 + 1)
            : attackPower;
    projectile.depthBias133 = static_cast<std::int8_t>(levelB * -0x0C);
    projectile.descriptorFlags02 =
        static_cast<std::uint16_t>(projectile.descriptorFlags02 | 0x1000u);
    projectile.halfword04 = 0x19;
    projectile.positionX20 = castPosition.x;
    projectile.positionZ24 = castPosition.y;
    projectile.positionY28 = castPosition.z;
    projectile.groundHeight4c = castPosition.z;
    projectile.previousGroundHeight50 = castPosition.z;
    projectile.facingRadians5c = (static_cast<std::size_t>(casterSlot) < kEntitySlotCount)
                                     ? pool.slot(static_cast<std::size_t>(casterSlot)).facingRadians5c
                                     : 0.0f;
    projectile.hitParameters198 = hitParameters;
    projectile.state60 = element;
    projectile.lightningTarget1ac = target;
    projectile.lightningCaster1ae = casterSlot;
    projectile.lightningLevel1b3 = levelB;
    projectile.lightningByte1b2 = 0;
    projectile.fadeRamp62 = 0;
    projectile.animationA0 = spell.projectileAnimation;
    FUN_00215e48_clear_hit_set(projectile);
    const float scale =
        spell.scaleWithLevel
            ? static_cast<float>(scaled) * kFGpffffa9e4_projectileScalePerLevel + 1.0f
            : 1.0f;
    projectile.scale14c = scale;
    projectile.scaleZ150 = scale;
    if (scaled == 5)
    {
      // +0x19B is byte 3 of the four the launch copied into +0x198, i.e. the
      // hit record's reaction. The original writes it on the spawn as well as
      // inside the sweep.
      projectile.hitParameters198 = (hitParameters & 0x00FFFFFFu) | (0x18u << 24);
    }
    if (casterSlot == 0 && target > 2 && static_cast<std::size_t>(target) < kEntitySlotCount &&
        (pool.slot(static_cast<std::size_t>(target)).effectFlags96 & 0x40u) != 0)
    {
      projectile.effectFlags96 = static_cast<std::uint8_t>(projectile.effectFlags96 | 0x40u);
    }

    FUN_002e9668_elemental_sweep(spell.handType, scaled, hitParameters, projectile, slot,
                                 castPosition, environment);
    return static_cast<std::int32_t>(slot);
  }

  // FUN_002e3110 / FUN_002e2048 / FUN_002dfd38 / FUN_002e0f80: the charge in
  // Orphen's hand. Line for line FUN_002deae8 above, with the three
  // differences named at the top of this block.
  void FUN_002e3110_elemental_hand(OriginalEntity &effect,
                                   const ActorEnvironment &environment,
                                   const ElementalSpellB &spell)
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
    effect.depthBias133 = -0x0C; // 0xF4
    effect.halfword08 = static_cast<std::uint16_t>(entryFlags08 | 0x4000u);
    // The bone index is taken negative here and negated again at the launch, so
    // FUN_0020DC88 always sees the positive one. 0x174 skips both halves.
    if (effect.attachBone194 > 0)
    {
      effect.attachBone194 = static_cast<std::int8_t>(-effect.attachBone194);
    }
    // And the effect parks itself at the origin, riding the caster's bone
    // rather than a world position, with the caster's facing.
    effect.positionX20 = 0.0f;
    effect.positionZ24 = 0.0f;
    effect.positionY28 = 0.0f;
    effect.facingRadians5c = caster.facingRadians5c;

    ActorEnvironment::BattleMemberView view;
    const std::uint32_t member = static_cast<std::uint32_t>(caster.byte95) - 1u;
    const bool haveBlock = caster.byte95 != 0 && environment.DAT_0031d7b0_battleMember &&
                           environment.DAT_0031d7b0_battleMember(member, view);

    std::int16_t step = static_cast<std::int16_t>(effect.animationA0);
    if (step != 2)
    {
      if (haveBlock && view.pendingAction0e == 0x0B)
      {
        effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 | 1u);
        FUN_00225bc8_set_animation(effect, 2);
      }
      const std::uint8_t action = haveBlock ? view.currentAction0f : 0;
      if (static_cast<std::uint8_t>(action + 0x74u) > 1u)
      {
        effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 | 1u);
        FUN_00225bc8_set_animation(effect, 2);
      }
      step = static_cast<std::int16_t>(effect.animationA0);
    }

    if (step == 0 || step == 1)
    {
      if (casterSlot == 0 && environment.FUN_002f1380_show_hit_effect &&
          environment.FUN_002493f0_spell_landing)
      {
        ActorEnvironment::BattleMemberView lead;
        const OriginalEntity &player = pool.slot(0);
        std::int32_t charge = 0;
        if (player.byte95 != 0 && environment.DAT_0031d7b0_battleMember &&
            environment.DAT_0031d7b0_battleMember(static_cast<std::uint32_t>(player.byte95) - 1u,
                                                  lead))
        {
          charge = lead.chargeTimer3c > 0x2580 ? 0x2580 : lead.chargeTimer3c;
        }
        orphen::ported::psm2::Vec3 landing{};
        environment.FUN_002493f0_spell_landing(0, landing);
        environment.FUN_002f1380_show_hit_effect(static_cast<float>(charge) / 1000.0f + 1.5f, 1.0f,
                                                 landing);
      }
      FUN_002da220_spell_light(effect, casterSlot, haveBlock ? view.chargeTimer3c : 0,
                               spell.lightRed, spell.lightGreen, spell.lightBlue, 1000,
                               environment);
    }

    if (step == 1)
    {
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
      effect.flags06 = static_cast<std::uint16_t>(effect.flags06 & 0xFFEFu);
      effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 & 0xFFFEu);
      // FUN_002660D0(caster): drop the caster's own light request. 0x174 does
      // not have this line; the four that do reach it only on animation 0.
    }
    else if (step == 2)
    {
      if ((effect.flags06 & 1u) != 0)
      {
        effect.parentSlot192 = casterIndex;
        effect.flags06 = static_cast<std::uint16_t>(effect.flags06 | 0x10u);
        effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 | 1u);
        effect.scale14c = 1.0f;
        effect.scaleZ150 = 1.0f;
        if (effect.lightSlot195 >= 0 && environment.DAT_00343888_lights != nullptr)
        {
          environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(effect.lightSlot195))
              .radius = 0.0f;
          effect.lightSlot195 = -1;
        }
      }
    }

    if (effect.state60 != 1)
    {
      return;
    }
    effect.flags06 = static_cast<std::uint16_t>(effect.flags06 & 0xFFEFu);
    effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 & 0xFFFEu);
    FUN_00225bc8_set_animation(effect, 2);
    effect.state60 = 0;

    // The bone point is the summon's anchor -- the launch's param_6, which only
    // the level-5 branch reads. The thrown projectile spawns at the landing
    // point below instead.
    orphen::ported::psm2::Vec3 summonAnchor{caster.positionX20, caster.positionZ24,
                                            caster.positionY28};
    if (environment.FUN_0020dc88_bone_point)
    {
      summonAnchor = environment.FUN_0020dc88_bone_point(
          casterSlot, static_cast<std::size_t>(-effect.attachBone194),
          orphen::ported::psm2::Vec3{0.0f, 0.0f, 0.0f});
    }

    orphen::ported::psm2::Vec3 castPosition{caster.positionX20, caster.positionZ24,
                                            caster.positionY28};
    std::int32_t target = -1;
    if (environment.FUN_002493f0_spell_landing)
    {
      target = environment.FUN_002493f0_spell_landing(casterSlot, castPosition);
    }
    const std::uint32_t hitParameters =
        environment.DAT_0031d3c8_battleTableWord
            ? environment.DAT_0031d3c8_battleTableWord(effect.hitParameters198)
            : 0;
    // The leading 1 is the element byte the launch parks in the projectile's
    // +0x60. Every one of the four passes the same 1; 0x174 has no such
    // argument and leaves its disc at 0.
    FUN_002e2d38_launch_elemental(spell, 1, effect.spawnParam94, effect.attackPower12c,
                                  static_cast<std::int16_t>(target), hitParameters,
                                  static_cast<std::int16_t>(casterSlot), summonAnchor, castPosition,
                                  environment);
  }

  // 0x002E3490 / 0x002E23C8 / 0x002E00B8 / 0x002E1300: the four projectiles'
  // whole behaviour, eight instructions each and no src/ file -- recovered from
  // SLUS_200.11. Free the entity when its animation comes round. Falcon's is
  // the only one that also forces animation 1 on the way out.
  void LAB_002e3490_elemental_projectile(OriginalEntity &effect,
                                         std::size_t slot,
                                         const ActorEnvironment &environment,
                                         bool forceAnimationOne)
  {
    if (forceAnimationOne)
    {
      effect.animationA0 = 1;
    }
    if ((effect.flags06 & 1u) != 0 && environment.entityPool != nullptr)
    {
      environment.entityPool->releaseSlot(slot);
    }
  }


  // ======================== the four other kind-12 elemental spells
  //
  // Bolt of Thunder, Feathers of the Hurricane, Smoke of Pain and Coldness of
  // Destruction: Hand of Pyro's shape four more times, and the only one of the
  // three families where the *projectile* is genuinely per-element.
  //
  //   spell 2  Bolt of Thunder           0x13A  FUN_002db548 / FUN_002db258 / 0x155 FUN_002db7d0
  //   spell 3  Feathers of the Hurricane 0x13B  FUN_002dbf48 / FUN_002dbc68 / 0x157 FUN_002dc1c8
  //   spell 4  Smoke of Pain             0x13C  FUN_002dc960 / FUN_002dc688 / 0x159 FUN_002dcc20
  //   spell 6  Coldness of Destruction   0x194  FUN_002dd358 / FUN_002dd078 / 0x195 FUN_002dd618
  //   (spell 5 Hand of Pyro              0x13D  FUN_002da8a0 / FUN_002dab70 / 0x15B FUN_002dae60)
  //
  // The hands come in two shapes and the launches in one; the four projectiles
  // share a skeleton -- sweep, burst-on-hit, life countdown, motion, successor,
  // free -- and differ only in how they steer and how many successors they
  // throw. The bursts cost nothing at all: types 0x170, 0x171, 0x172 and 0x196
  // are `j 0x2db230`, the same two instructions Hand of Pyro's 0x173 runs, so
  // they are four extra labels on a case that is already here.
  enum class Kind12Element
  {
    Bolt,
    Feathers,
    Smoke,
    Cold,
  };

  struct Kind12Spell
  {
    Kind12Element element;
    std::int16_t handType;
    std::int16_t projectileType;
    std::int16_t burstType;
    // The hand. Bolt and Feathers take the bone index negative and ride the
    // caster's facing; Smoke and Cold park a fixed pose the way Hand of Pyro
    // does and tilt it by the caster's class.
    bool negateBone;
    // The launch.
    std::uint16_t successorTicks; // FUN_00248E48's argument for +0x62
    std::uint16_t lifeTicks;      // and for +0x1C4
    float unaimedSpeed;           // +0x1BC when there is nothing to aim at
    float aimedSpeedDivisor;      // the flight-time divisor; 0 means "use unaimedSpeed"
    // The projectile.
    float burstFacingOffset; // added to the burst's facing when it lands
    float homeRate;          // FUN_0023A320's cap while it tracks
    std::array<float, 4> chainYaw; // the spread, indexed by chain - 1
  };

  // Every homing rate in all four is the same 0.00436332 -- a quarter degree a
  // tick -- and every burst offset is pi. They are sixteen separate words in the
  // executable and they get sixteen slots here for the same reason the enemies'
  // turn rates do.
  inline constexpr Kind12Spell kKind12Spells[4]{
      {Kind12Element::Bolt, 0x13A, 0x155, 0x170, true, 1, 0x6C, 0.00347222f, 0.00347222f,
       3.1415927f, 0.00436332f, {{0.0f, 0.0f, 0.0f, 0.0f}}},
      {Kind12Element::Feathers, 0x13B, 0x157, 0x171, true, 0x0F, 0x6C, 0.00347222f, 0.00347222f,
       3.1415927f, 0.00436332f, {{2.79253f, -2.79253f, 1.74533f, -1.74533f}}},
      {Kind12Element::Smoke, 0x13C, 0x159, 0x172, false, 0x28, 0x1B0, 0.000868056f, 0.0f,
       3.1415927f, 0.00436332f, {{1.39626f, -1.39626f, 1.39626f, -1.39626f}}},
      {Kind12Element::Cold, 0x194, 0x195, 0x196, false, 0x1E, 0x6C, 0.00347222f, 0.00347222f,
       3.1415927f, 0.00436332f, {{2.79253f, -2.79253f, 2.61799f, -2.61799f}}},
  };

  const Kind12Spell *kind12ForHand(std::int16_t typeId)
  {
    for (const auto &row : kKind12Spells)
    {
      if (row.handType == typeId)
      {
        return &row;
      }
    }
    return nullptr;
  }

  const Kind12Spell *kind12ForProjectile(std::int16_t typeId)
  {
    for (const auto &row : kKind12Spells)
    {
      if (row.projectileType == typeId)
      {
        return &row;
      }
    }
    return nullptr;
  }

  // Coldness of Destruction alone gives itself a hop as the spread ends, and
  // bumps its facing once more on the way out.
  inline constexpr float kDAT_00354904_coldHop = 0.038f;
  inline constexpr float kDAT_00354910_coldDeathYaw = 0.5235990f;
  // uGpffffa8e0 / DAT_003548b8, the pose Smoke and Cold park, and the two class
  // tilts that go with it. Hand of Pyro's class-4 arm also writes +0x158; these
  // two do not.
  inline constexpr float kSmokeColdPoseFacing = 1.5707964f;
  inline constexpr float kSmokeColdTiltClass1 = 0.6981317f;
  inline constexpr float kSmokeColdTiltClass4 = 2.6179900f;
  // The light all four hang on the caster's hand -- grey where Hand of Pyro's
  // is orange -- and the one the projectile carries.
  inline constexpr std::uint8_t kKind12LightGrey = 0x42;
  // DAT_00354880 / DAT_003548AC / fGpffffa968 / DAT_0035490C, all a full turn:
  // the numerator of the degrees-to-radians the spread and trim are expressed
  // in. Four words, one value, kept as one name here because all four uses are
  // inside this block.
  inline constexpr float kKind12Tau = 6.2831840515136719f;

  // FUN_002D6BD0: one cue for the whole volley, picked off the first victim's
  // pending damage. Under 4 is 0xE2, under 11 is 0xE3, anything more is 0xE4.
  void FUN_002d6bd0_hit_cue(const ActorEnvironment &environment, const OriginalEntity &source)
  {
    if (environment.hitTest == nullptr || environment.hitTest->DAT_003151c8_hitList == nullptr ||
        environment.entityPool == nullptr || !environment.FUN_00267d38_playSound)
    {
      return;
    }
    const auto &hitList = *environment.hitTest->DAT_003151c8_hitList;
    for (std::size_t index = 0; index < hitList.size() && index < 256u; ++index)
    {
      const std::uint16_t victim = hitList[index];
      if (victim == 0 || static_cast<std::size_t>(victim) >= kEntitySlotCount)
      {
        continue;
      }
      const std::int16_t damage = static_cast<std::int16_t>(
          environment.entityPool->slot(static_cast<std::size_t>(victim)).pendingDamageBe);
      if (damage <= 0)
      {
        continue;
      }
      environment.FUN_00267d38_playSound(damage < 4 ? 0xE2 : (damage < 0x0B ? 0xE3 : 0xE4), source);
      return;
    }
  }

  // FUN_002DB7D0:29-40 and its three twins: when the player cast it, offer every
  // victim to the camera and key the rumble.
  //
  // FUN_0023C220 is the offer -- it parks the victim in DAT_00354E84 so
  // FUN_0023C340 swings the camera onto it -- and it is **not ported**: the
  // battle camera's reaction shot is a whole subsystem this does not touch, and
  // getting it half-right would move the camera at the wrong moments. The cue
  // and the burst are what the volley is actually made of, and both are here.
  void kind12_landed(OriginalEntity &projectile,
                     std::size_t slot,
                     const Kind12Spell &spell,
                     const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr || environment.descriptors == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    // FUN_0023C220 per victim and FUN_0023BBD8(0, 6), the pad rumble, are the
    // caster == 0 half and are not ported; FUN_002D6BD0 is unconditional and
    // is what the volley actually sounds like.
    FUN_002d6bd0_hit_cue(environment, projectile);

    // The burst. It inherits the projectile's position and the *negated* +0x154,
    // so the splash leans the opposite way to the shot that made it.
    const std::size_t burstSlot = pool.FUN_00265e28_allocate_and_initialize(
        static_cast<std::uint32_t>(spell.burstType), *environment.descriptors);
    if (burstSlot < kEntitySlotCount)
    {
      auto &burst = pool.slot(burstSlot);
      burst.positionX20 = projectile.positionX20;
      burst.positionZ24 = projectile.positionZ24;
      burst.positionY28 = projectile.positionY28;
      if (spell.element == Kind12Element::Smoke)
      {
        // Smoke of Pain alone carries the ground height across too.
        burst.groundHeight4c = projectile.groundHeight4c;
        burst.previousGroundHeight50 = projectile.groundHeight4c;
      }
      burst.facingRadians5c = projectile.facingRadians5c + spell.burstFacingOffset;
      burst.rotationX154 = -projectile.rotationX154;
      FUN_00225bc8_set_animation(burst, 0);
    }
    pool.releaseSlot(slot);
  }

  // FUN_002DC1C8:120-155, shared verbatim with FUN_002DD618: the fan.
  //
  // Fill all five entries with the shot's own target, then put pool slot 2 in
  // the second if it is a live battle participant, then walk 3..255 taking
  // every live entity whose +0x96 bit 0 is up -- the bit FUN_0023F8B8 sets when
  // something is bound into an actor record -- until six are collected. The
  // volley then throws one successor per charge level, each aimed at the next
  // entry, so a full charge fans across the whole field and a tap does not.
  std::array<std::int16_t, 5> kind12_fan_targets(const EntityPool &pool, std::int16_t ownTarget)
  {
    std::array<std::int16_t, 5> targets{};
    targets.fill(ownTarget);

    std::size_t filled = 1;
    std::size_t candidate = 3;
    const bool slot2Live = pool.slotCount() > 2 && pool.status(2) != SlotStatus::Free;
    if (slot2Live && (pool.slot(2).battleFlags96 & 1u) != 0)
    {
      targets[1] = 2;
      filled = 2;
    }
    else if (!slot2Live)
    {
      candidate = 2;
    }
    for (; candidate < pool.slotCount() && candidate < 0x100u; ++candidate)
    {
      if (pool.status(candidate) == SlotStatus::Free)
      {
        continue;
      }
      if ((pool.slot(candidate).battleFlags96 & 1u) == 0)
      {
        continue;
      }
      targets[filled] = static_cast<std::int16_t>(candidate);
      ++filled;
      if (filled >= targets.size())
      {
        break;
      }
    }
    return targets;
  }

  // The fan itself: one successor per charge level, each at the next entry of
  // the target list, thrown from where this shot is standing and owned by it.
  void kind12_throw_fan(OriginalEntity &shot,
                        std::size_t slot,
                        const Kind12Spell &spell,
                        const ActorEnvironment &environment);

  // FUN_002DB258 / FUN_002DBC68 / FUN_002DC688 / FUN_002DD078: the launch, and
  // the one function of the three that really is the same four times.
  //
  // Structurally FUN_002DAB70 with three differences: the light is grey rather
  // than orange, the successor timer is a per-element constant rather than
  // `8 - charge`, and Bolt of Thunder parks its chain and charge two bytes
  // further up so it can keep +0x1C6 as the aimed shot's flight time.
  std::int32_t FUN_002db258_launch_kind12(const Kind12Spell &spell,
                                          std::uint8_t chainIndex,
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
    const std::size_t spawned = pool.FUN_00265e28_allocate_and_initialize(
        static_cast<std::uint32_t>(spell.projectileType), *environment.descriptors);
    if (spawned >= kEntitySlotCount)
    {
      return -1;
    }
    auto &shot = pool.slot(spawned);

    if (chainIndex == 0)
    {
      shot.effectFlags96 = static_cast<std::uint8_t>(shot.effectFlags96 | 0x40u);
    }
    shot.attackPower12c = attackPower;
    shot.fireballOriginX19c = originX;
    shot.positionX20 = originX;
    shot.fireballOriginZ1a0 = originZ;
    shot.positionZ24 = originZ;
    shot.fireballOriginY1a4 = originY;
    shot.positionY28 = originY;
    shot.hitParameters198 = hitParameters;
    shot.fireballTarget1c0 = target;
    shot.fireballCaster1c2 = casterSlot;
    if (spell.element == Kind12Element::Bolt)
    {
      shot.boltChain1c8 = chainIndex;
      shot.boltCharge1c9 = chargeLevel;
    }
    else
    {
      shot.fireballChain1c6 = chainIndex;
      shot.fireballCharge1c7 = chargeLevel;
    }
    shot.fadeRamp62 = 0;
    shot.animationA0 = 0;

    // Its own light. FUN_0023EB20 is the same two-stage allocator
    // FUN_002DA220 uses, and FUN_002660D0 puts it on the entity.
    if (environment.DAT_00343888_lights != nullptr)
    {
      if (shot.lightSlot195 < 0)
      {
        const std::int32_t high = environment.DAT_00343888_lights->FUN_00266008_allocateFromThree();
        const std::int32_t allocated =
            high >= 0 ? high : environment.DAT_00343888_lights->FUN_00266050_allocateFromZero();
        shot.lightSlot195 = static_cast<std::int8_t>(allocated);
      }
      if (shot.lightSlot195 >= 0)
      {
        auto &light =
            environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(shot.lightSlot195));
        light.radius = 2.0f;
        light.red = kKind12LightGrey;
        light.green = kKind12LightGrey;
        light.blue = kKind12LightGrey;
        light.x = shot.positionX20;
        light.y = shot.positionZ24;
        light.z = shot.positionY28;
      }
    }

    std::uint8_t spark = chargeLevel;
    if (spark != 0)
    {
      if (spark == chainIndex)
      {
        // The last link: no successor timer, which is what ends the chain.
        if (spark == 5)
        {
          shot.animationA0 = 0;
          spark = 0x18;
        }
        else
        {
          shot.fireballSparkId19b = static_cast<std::uint8_t>(spark + 0x14);
        }
      }
      else
      {
        shot.fadeRamp62 =
            static_cast<std::uint16_t>(FUN_00248e48_arm_timer(spell.successorTicks));
      }
    }
    if (spark == 5)
    {
      shot.fireballSparkId19b = 0x18;
    }

    shot.facingRadians5c = (static_cast<std::size_t>(casterSlot) < kEntitySlotCount)
                               ? pool.slot(static_cast<std::size_t>(casterSlot)).facingRadians5c
                               : 0.0f;
    shot.fireballLife1c4 = static_cast<std::uint16_t>(FUN_00248e48_arm_timer(spell.lifeTicks));

    const bool aimed = target > 1 && static_cast<std::size_t>(target) < kEntitySlotCount &&
                       pool.slot(static_cast<std::size_t>(target)).typeId00 != 0;
    if (!aimed)
    {
      shot.fireballSpeed1bc = spell.unaimedSpeed;
      shot.fireballRise1b0 = 0.0f;
    }
    else
    {
      const auto &victim = pool.slot(static_cast<std::size_t>(target));
      const float dx = victim.positionX20 - originX;
      const float dz = victim.positionZ24 - originZ;
      const float dy = victim.positionY28 - originY;
      // FUN_00216648 over the divisor: the flight time in ticks at the
      // element's fixed speed. Smoke of Pain is the odd one -- it divides by its
      // own speed and then *keeps* that speed rather than solving for it, so its
      // shot always travels at the same rate and only its climb varies.
      const float divisor =
          spell.aimedSpeedDivisor != 0.0f ? spell.aimedSpeedDivisor : spell.unaimedSpeed;
      const float flight = std::sqrt(dx * dx + dz * dz + dy * dy) / divisor;
      if (flight > 0.0f)
      {
        shot.fireballRise1b0 =
            ((victim.positionY28 + victim.height58 * 0.5f) - originY) / flight;
        shot.fireballSpeed1bc = spell.element == Kind12Element::Smoke
                                    ? spell.unaimedSpeed
                                    : std::sqrt(dx * dx + dz * dz) / flight;
      }
      if (spell.element == Kind12Element::Bolt)
      {
        // +0x1C6 as a short: the costed flight plus 0xF00. The projectile
        // counts it down and stops homing when it reaches zero.
        shot.boltFlight1c6 =
            static_cast<std::uint16_t>(static_cast<std::int32_t>(flight) + 0xF00);
      }
    }

    if (environment.FUN_00267d38_playSound)
    {
      environment.FUN_00267d38_playSound(0xCC, shot);
    }
    return static_cast<std::int32_t>(spawned);
  }

  // FUN_002DB548 / FUN_002DBF48 / FUN_002DC960 / FUN_002DD358: the charge in
  // Orphen's hand. Hand of Pyro's body with a per-element pose and light.
  void FUN_002db548_kind12_hand(OriginalEntity &effect,
                                const ActorEnvironment &environment,
                                const Kind12Spell &spell)
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
    effect.depthBias133 = -0x0C; // 0xF4
    effect.halfword08 = static_cast<std::uint16_t>(entryFlags08 | 0x4000u);
    if (spell.negateBone)
    {
      // Bolt and Feathers take the bone negative here and negate it again at
      // the throw, so FUN_0020DC88 always sees the positive index.
      if (effect.attachBone194 > 0)
      {
        effect.attachBone194 = static_cast<std::int8_t>(-effect.attachBone194);
      }
      effect.facingRadians5c = caster.facingRadians5c;
    }
    else
    {
      effect.facingRadians5c = 0.0f;
    }

    ActorEnvironment::BattleMemberView view;
    const std::uint32_t member = static_cast<std::uint32_t>(caster.byte95) - 1u;
    const bool haveBlock = caster.byte95 != 0 && environment.DAT_0031d7b0_battleMember &&
                           environment.DAT_0031d7b0_battleMember(member, view);

    std::int16_t step = static_cast<std::int16_t>(effect.animationA0);
    if (step != 2)
    {
      if (haveBlock && view.pendingAction0e == 0x0B)
      {
        effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 | 1u);
        FUN_00225bc8_set_animation(effect, 2);
      }
      // `1 < (byte)(current + 0x76)` -- anything outside the kind-12 action
      // pair 0x8A and 0x8B, i.e. the caster has stopped casting.
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
      if (!spell.negateBone)
      {
        // Smoke and Cold park the pose here rather than riding the caster.
        effect.facingRadians5c = kSmokeColdPoseFacing;
        if (haveBlock && view.characterClass == 1)
        {
          effect.rotationX154 = kSmokeColdTiltClass1;
        }
        else if (haveBlock && view.characterClass == 4)
        {
          effect.rotationX154 = kSmokeColdTiltClass4;
        }
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
      // The hold. Grey where Hand of Pyro's is orange, same 500 base radius.
      FUN_002da220_spell_light(effect, casterSlot, haveBlock ? view.chargeTimer3c : 0,
                               kKind12LightGrey, kKind12LightGrey, kKind12LightGrey, 500,
                               environment);
    }
    else if (step == 2)
    {
      if ((effect.flags06 & 1u) != 0)
      {
        effect.flags06 = static_cast<std::uint16_t>(effect.flags06 | 0x10u);
        effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 | 1u);
        effect.scale14c = 1.0f;
        effect.scaleZ150 = 1.0f;
        // FUN_00266098: give the light slot back.
        if (effect.lightSlot195 >= 0 && environment.DAT_00343888_lights != nullptr)
        {
          environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(effect.lightSlot195))
              .radius = 0.0f;
          effect.lightSlot195 = -1;
        }
      }
    }

    // **The throw.** +0x60 is 1 for one frame, written by FUN_0024BAE0 when the
    // release animation reaches its +0xAA bit 0x100 marker.
    if (effect.state60 != 1)
    {
      return;
    }
    effect.flags06 = static_cast<std::uint16_t>(effect.flags06 & 0xFFEFu);
    effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 & 0xFFFEu);
    FUN_00225bc8_set_animation(effect, 2);
    effect.state60 = 0;

    orphen::ported::psm2::Vec3 origin{effect.positionX20, effect.positionZ24, effect.positionY28};
    if (environment.FUN_0020dc88_bone_point)
    {
      const std::size_t bone = static_cast<std::size_t>(
          spell.negateBone ? -effect.attachBone194 : effect.attachBone194);
      origin = environment.FUN_0020dc88_bone_point(casterSlot, bone,
                                                   orphen::ported::psm2::Vec3{0.0f, 0.0f, 0.0f});
    }
    const std::uint32_t hitParameters =
        environment.DAT_0031d3c8_battleTableWord
            ? environment.DAT_0031d3c8_battleTableWord(effect.hitParameters198)
            : 0;
    // FUN_002493B8, not FUN_002493F0: the kind-12 arm throws at the control
    // block's target slot rather than at a landing spot.
    FUN_002db258_launch_kind12(spell, 0, effect.spawnParam94, effect.attackPower12c,
                               haveBlock ? view.target : -1, hitParameters, origin.x, origin.y,
                               origin.z, static_cast<std::int16_t>(casterSlot), environment);
  }

  void kind12_throw_fan(OriginalEntity &shot,
                        std::size_t slot,
                        const Kind12Spell &spell,
                        const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    const std::uint8_t charge = shot.fireballCharge1c7;
    if (charge == 0)
    {
      return;
    }
    // Coldness stamps 0x50 first so the fan happens once whichever of its two
    // exits reaches it -- the sweep or the wall.
    if (spell.element == Kind12Element::Cold)
    {
      shot.fireballChain1c6 = 0x50;
    }
    const auto targets = kind12_fan_targets(pool, shot.fireballTarget1c0);
    for (std::size_t index = 0; index < charge && index < targets.size(); ++index)
    {
      FUN_002db258_launch_kind12(spell, static_cast<std::uint8_t>(index + 1), charge,
                                 shot.attackPower12c, targets[index], shot.hitParameters198,
                                 shot.positionX20, shot.positionZ24, shot.positionY28,
                                 static_cast<std::int16_t>(slot), environment);
    }
  }

  // FUN_002DB7D0 / FUN_002DC1C8 / FUN_002DCC20 / FUN_002DD618: the projectile.
  //
  // The skeleton is the same four times -- keep the light on, clear the damage
  // fields, sweep, burst and die on a hit, run the life down, steer, move,
  // throw the successor, die on a wall -- and the steering is what makes each
  // spell look like itself:
  //
  //   Bolt of Thunder   the chain fans by a *time-scaled* yaw, so the bolts
  //                     splay wider the longer they have been flying, and each
  //                     one homes on a +0x1B4 that tracks the target
  //                     separately from the yaw it is drawn at
  //   Feathers          the chain takes a fixed yaw step, flies blind until it
  //                     has turned, then homes; the volley fans across up to
  //                     five different enemies rather than stacking on one
  //   Smoke of Pain     the chain is a fixed yaw *bias* held for the whole
  //                     flight, so the shots corkscrew around the aim line, and
  //                     each one throws exactly one successor and then halves
  //                     its own speed
  //   Coldness          Feathers' fan, plus a hop as the spread ends, and the
  //                     fan is thrown when it *lands* rather than on a timer
  //
  // The pitch term every one of them computes is `(spread * tau / 360 / N) *
  // elapsed`, and in three of the four the spread is multiplied by a literal
  // zero -- the compiler kept the multiply, so the trim is always zero for
  // Feathers, Smoke and Cold and only Bolt actually climbs or dips.
  void FUN_002db7d0_kind12_projectile(OriginalEntity &shot,
                                      std::size_t slot,
                                      const ActorEnvironment &environment,
                                      const Kind12Spell &spell)
  {
    if (environment.entityPool == nullptr || environment.descriptors == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    const std::uint16_t ticks = static_cast<std::uint16_t>(environment.frameTicks);
    const std::int16_t target = shot.fireballTarget1c0;
    const std::uint8_t chain =
        spell.element == Kind12Element::Bolt ? shot.boltChain1c8 : shot.fireballChain1c6;
    const std::uint8_t charge =
        spell.element == Kind12Element::Bolt ? shot.boltCharge1c9 : shot.fireballCharge1c7;

    // FUN_002660D0: the light rides the entity.
    if (shot.lightSlot195 >= 0 && environment.DAT_00343888_lights != nullptr)
    {
      auto &light =
          environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(shot.lightSlot195));
      light.x = shot.positionX20;
      light.y = shot.positionZ24;
      light.z = shot.positionY28;
    }
    shot.hitSourceC0 = 0;
    shot.hitFlagsC2 = 0;
    shot.pendingDamageBe = 0;
    shot.freezeTimerBd = 0;
    shot.halfword08 = static_cast<std::uint16_t>(shot.halfword08 | 0x4000u);

    // Coldness of Destruction is the one that does not sweep every frame: while
    // it still owes a successor *and* has its hop, it skips the test entirely.
    const bool sweepThisFrame = spell.element != Kind12Element::Cold ||
                                shot.fadeRamp62 == 0 || shot.verticalVelocity44 == 0.0f;
    if (sweepThisFrame && environment.hitTest != nullptr)
    {
      FUN_00215e48_clear_hit_set(shot);
      const std::int8_t contacts = FUN_002148a8_swept_hit_test(
          shot, slot, orphen::ported::resource::HitParameters::unpack(shot.hitParameters198),
          *environment.hitTest);
      if (contacts != 0)
      {
        if (spell.element == Kind12Element::Cold && chain == 0)
        {
          kind12_throw_fan(shot, slot, spell, environment);
        }
        kind12_landed(shot, slot, spell, environment);
        return;
      }
    }

    // Bolt of Thunder's first frame: its drawn yaw is rebuilt from the spread
    // every frame, so the launch angle is moved into +0x1B4 and +0x5C is zeroed
    // before anything reads it.
    if (spell.element == Kind12Element::Bolt &&
        shot.fireballLife1c4 == static_cast<std::uint16_t>(FUN_00248e48_arm_timer(spell.lifeTicks)))
    {
      shot.fireballBaseFacing1b4 = shot.facingRadians5c;
      shot.facingRadians5c = 0.0f;
    }

    const std::uint16_t life = FUN_00248e58_step_timer(shot.fireballLife1c4, ticks);
    shot.fireballLife1c4 = life;
    if (life == 0)
    {
      pool.releaseSlot(slot);
      return;
    }
    const float elapsed = static_cast<float>(static_cast<std::int16_t>(
        static_cast<std::uint16_t>(spell.lifeTicks * 16u) - life));
    const float pitchDivisor = spell.element == Kind12Element::Smoke ? 13824.0f : 3456.0f;

    const bool targetAlive = target > 0 && static_cast<std::size_t>(target) < kEntitySlotCount &&
                             pool.slot(static_cast<std::size_t>(target)).typeId00 > 0 &&
                             static_cast<std::int16_t>(
                                 pool.slot(static_cast<std::size_t>(target)).staggerTimer12a) > 0;
    const auto bearingToTarget = [&]() {
      const auto &victim = pool.slot(static_cast<std::size_t>(target));
      return std::atan2(victim.positionZ24 - shot.positionZ24,
                        victim.positionX20 - shot.positionX20);
    };

    float pitchSpread = 0.0f;
    float drawYaw = shot.facingRadians5c;

    switch (spell.element)
    {
    case Kind12Element::Bolt:
    {
      // :74-102. The fan only opens once the shot is 0x144 ticks old, and it is
      // a *rate*: the yaw is re-derived from `spread * elapsed` every frame, so
      // the three bolts keep spreading apart the whole way out.
      float yawSpread = 0.0f;
      if (elapsed > -1404.0f) // -0x57C
      {
        if (chain == 0)
        {
          pitchSpread = charge == 5 ? 4.0f : 0.0f;
        }
        else
        {
          shot.fireballSpeed1bc = 0.00694444f; // DAT_0035487C, the chain's own speed
          if (chain == 1)
          {
            if (charge != 1)
            {
              yawSpread = 35.0f; // 0x23
            }
            else
            {
              pitchSpread = -5.0f;
            }
          }
          else if (chain == 2)
          {
            yawSpread = -35.0f;
          }
          else
          {
            pitchSpread = chain == 3 ? -5.0f : 0.0f;
          }
        }
      }
      // :104-107. The homing gate is three-way: a dead target, or the flight
      // budget at +0x1C6 running out, ends the bolt outright.
      if (!targetAlive || shot.boltFlight1c6 == 0)
      {
        pool.releaseSlot(slot);
        return;
      }
      shot.facingRadians5c =
          ((yawSpread * kKind12Tau) / 360.0f / 3456.0f) * elapsed +
          shot.fireballBaseFacing1b4;
      drawYaw = shot.facingRadians5c;
      const float bearing = bearingToTarget();
      const float turn = FUN_0023a320_approach_angle(shot.fireballBaseFacing1b4, bearing,
                                                     static_cast<float>(ticks) * spell.homeRate);
      shot.fireballBaseFacing1b4 = turn == 0.0f ? bearing : shot.fireballBaseFacing1b4 + turn;
      const std::int32_t remaining =
          static_cast<std::int32_t>(shot.boltFlight1c6) - static_cast<std::int32_t>(ticks);
      shot.boltFlight1c6 = static_cast<std::uint16_t>(remaining);
      if (static_cast<std::int16_t>(shot.boltFlight1c6) < 0)
      {
        shot.boltFlight1c6 = 0;
      }
      break;
    }

    case Kind12Element::Feathers:
    case Kind12Element::Cold:
    {
      // :66-92. One fixed yaw step per chain position, then the chain index is
      // pushed past 100 so the step happens exactly once; from then on the
      // shot homes. Coldness also gives itself a hop on the same frame.
      std::uint32_t chainNow = chain;
      if (chainNow != 0)
      {
        if (chainNow >= 1 && chainNow <= 4)
        {
          shot.facingRadians5c += spell.chainYaw[chainNow - 1];
        }
        if (chainNow < 10)
        {
          chainNow += 100;
          shot.fireballChain1c6 = static_cast<std::uint8_t>(shot.fireballChain1c6 + 100);
          if (spell.element == Kind12Element::Cold)
          {
            shot.verticalVelocity44 = kDAT_00354904_coldHop;
          }
        }
      }
      if (chainNow >= 100 && targetAlive)
      {
        const float bearing = bearingToTarget();
        const float turn = FUN_0023a320_approach_angle(shot.facingRadians5c, bearing,
                                                       static_cast<float>(ticks) * spell.homeRate);
        shot.facingRadians5c = turn == 0.0f ? bearing : shot.facingRadians5c + turn;
      }
      drawYaw = shot.facingRadians5c;
      break;
    }

    case Kind12Element::Smoke:
    {
      // :64-104. The bias is held for the whole flight rather than folded into
      // the facing, so the shot flies at an angle to the line it is steering
      // along -- which is what makes the volley corkscrew.
      shot.fireballBaseFacing1b4 =
          chain >= 1 && chain <= 4 ? spell.chainYaw[chain - 1] : 0.0f;
      if (target >= 1 && targetAlive)
      {
        const float bearing = bearingToTarget();
        const float turn =
            FUN_0023a320_approach_angle(shot.facingRadians5c + shot.fireballBaseFacing1b4, bearing,
                                        static_cast<float>(ticks) * spell.homeRate);
        shot.facingRadians5c = turn == 0.0f ? bearing : shot.facingRadians5c + turn;
      }
      drawYaw = shot.facingRadians5c + shot.fireballBaseFacing1b4;
      break;
    }
    }

    // The motion every one of them shares.
    shot.fireballVelX1a8 = shot.fireballSpeed1bc * std::cos(drawYaw);
    shot.fireballVelZ1ac = shot.fireballSpeed1bc * std::sin(drawYaw);
    const float horizontal = std::sqrt(shot.fireballVelX1a8 * shot.fireballVelX1a8 +
                                       shot.fireballVelZ1ac * shot.fireballVelZ1ac);
    shot.fireballRiseOffset1b8 =
        ((pitchSpread * kKind12Tau) / 360.0f / pitchDivisor) * elapsed;
    shot.desiredDeltaY38 =
        shot.fireballRise1b0 * static_cast<float>(ticks) + shot.fireballRiseOffset1b8;
    shot.rotationX154 = std::atan2(-shot.fireballRise1b0, horizontal);
    shot.desiredDeltaX30 = shot.fireballVelX1a8 * static_cast<float>(ticks);
    shot.desiredDeltaZ34 = shot.fireballVelZ1ac * static_cast<float>(ticks);

    // The successor. Bolt and Smoke throw exactly one; Feathers throws the fan.
    if (shot.fadeRamp62 != 0)
    {
      shot.fadeRamp62 = FUN_00248e58_step_timer(shot.fadeRamp62, ticks);
      if (shot.fadeRamp62 == 0)
      {
        switch (spell.element)
        {
        case Kind12Element::Bolt:
          // From the launch origin, not from here, and carrying the caster on.
          FUN_002db258_launch_kind12(spell, static_cast<std::uint8_t>(chain + 1), charge,
                                     shot.attackPower12c, target, shot.hitParameters198,
                                     shot.fireballOriginX19c, shot.fireballOriginZ1a0,
                                     shot.fireballOriginY1a4, shot.fireballCaster1c2, environment);
          break;
        case Kind12Element::Feathers:
          if (chain == 0)
          {
            kind12_throw_fan(shot, slot, spell, environment);
          }
          break;
        case Kind12Element::Smoke:
          if (chain < 4)
          {
            FUN_002db258_launch_kind12(spell, static_cast<std::uint8_t>(chain + 1), charge,
                                       shot.attackPower12c, target, shot.hitParameters198,
                                       shot.positionX20, shot.positionZ24, shot.positionY28,
                                       static_cast<std::int16_t>(slot), environment);
          }
          // Every shot but the first halves its own speed once it has handed
          // the chain on, so the tail of the volley trails behind the head.
          if (chain != 0)
          {
            shot.desiredDeltaX30 *= 0.5f;
            shot.desiredDeltaZ34 *= 0.5f;
            shot.desiredDeltaY38 *= 0.5f;
          }
          break;
        case Kind12Element::Cold:
          // Cold throws its fan when it lands, not on the timer.
          break;
        }
      }
    }

    // +0x0C bits 0x4066: a wall, the ceiling or the floor.
    if ((shot.collisionFlags0c & 0x4066u) != 0)
    {
      if (spell.element == Kind12Element::Cold)
      {
        if (chain == 0)
        {
          kind12_throw_fan(shot, slot, spell, environment);
        }
        shot.facingRadians5c += kDAT_00354910_coldDeathYaw;
      }
      pool.releaseSlot(slot);
    }
  }
  // ============================================ the three shield barriers
  //
  // LAB_002DE0B8, the behaviour of types **0x127, 0x143 and 0x144** -- Shield
  // of Inferno, Shield of Immunity and Armor of Purity. One body, three
  // entries: 0x002DE0A8 and 0x002DE0B0 are two-instruction `j` thunks for the
  // first two types and 0x144 enters the body directly, which is why the
  // dispatch table has three different addresses for one function.
  //
  // **It is a Ghidra LAB with no src/ file and no JP counterpart** -- neither
  // thunk is reached by a `jal`, only through the pointer table, so no function
  // was ever created. Recovered from SLUS_200.11 at 0x002DE0B8..0x002DE36C,
  // 172 instructions.
  //
  // The animation order is **1 -> 0 -> 2**, not 0 -> 1 -> 2, and that matters:
  // state 115 spawns the barrier on animation 1 (FUN_0024BD30's
  // FUN_00248EE0(effect, 1)) and ends the cast the moment it reads animation 2
  // back. 1 is the rise, 0 is the hold, 2 is the drop. With the behaviour
  // absent the entity simply sat at whatever it spawned with, so all three
  // shield demos in s14_e031 released instantly with nothing on screen.
  //
  // The `shield_of_immunity` save state has slot 10 as a type 0x143 on
  // animation 0 with the player in state 115 -- the hold, which is where a
  // barrier spends its life.
  void LAB_002de0b8_shield_barrier(OriginalEntity &barrier,
                                   std::size_t slot,
                                   const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    // +0x94 is the caster's pool slot, stamped by state 115 on the spawn.
    const std::size_t ownerSlot = static_cast<std::size_t>(barrier.spawnParam94);
    if (ownerSlot >= kEntitySlotCount)
    {
      return;
    }
    OriginalEntity &owner = pool.slot(ownerSlot);

    // :14-40. While the barrier is not already dropping, watch the caster's
    // current action: `(action + 0x72) < 2` is true only for 0x8E and 0x8F, the
    // shield pair. Anything else means the cast is over, so hide and drop.
    if (barrier.animationA0 != 2)
    {
      ActorEnvironment::BattleMemberView view;
      const std::uint32_t member = static_cast<std::uint32_t>(owner.byte95) - 1u;
      const bool haveBlock = owner.byte95 != 0 && environment.DAT_0031d7b0_battleMember &&
                             environment.DAT_0031d7b0_battleMember(member, view);
      const std::uint8_t action = haveBlock ? view.currentAction0f : 0;
      if (static_cast<std::uint8_t>(action + 0x72u) >= 2u)
      {
        barrier.halfword08 = static_cast<std::uint16_t>(barrier.halfword08 | 1u);
        FUN_00225bc8_set_animation(barrier, 2);
      }
    }

    // :42-63. It rides the caster, half a unit up, every frame and in every
    // animation.
    barrier.positionX20 = owner.positionX20;
    barrier.positionZ24 = owner.positionZ24;
    const float top = owner.positionY28 + 0.5f;
    barrier.positionY28 = top;
    barrier.groundHeight4c = top;
    barrier.previousGroundHeight50 = top;

    const std::int16_t animation = static_cast<std::int16_t>(barrier.animationA0);
    if (animation == 1)
    {
      // The rise. The cue is keyed once, on the first frame, and only 0x143
      // also takes a depth bias -- it is the one that draws in front.
      if (barrier.barrierCued1a0 == 0)
      {
        if (barrier.typeId00 == 0x143)
        {
          barrier.depthBias133 = -10;
          if (environment.FUN_00267d38_playSound)
          {
            environment.FUN_00267d38_playSound(0xD5, barrier);
          }
        }
        else if (barrier.typeId00 == 0x127)
        {
          if (environment.FUN_00267d38_playSound)
          {
            environment.FUN_00267d38_playSound(0xD3, barrier);
          }
        }
        else if (barrier.typeId00 == 0x144)
        {
          if (environment.FUN_00267d38_playSound)
          {
            environment.FUN_00267d38_playSound(0xD1, barrier);
          }
        }
        barrier.barrierCued1a0 = 1;
      }
      const std::uint16_t flags06 = static_cast<std::uint16_t>(barrier.flags06 & 0xFFEFu);
      barrier.barrierRaised19c = 1;
      barrier.halfword08 = static_cast<std::uint16_t>(barrier.halfword08 & 0xFFFEu);
      barrier.flags06 = flags06;
      barrier.facingRadians5c = owner.facingRadians5c;
      if ((flags06 & 1u) != 0)
      {
        // The rise is done: hold.
        FUN_00225bc8_set_animation(barrier, 0);
      }
    }
    else if (animation == 0)
    {
      // The hold. One frame after the hold animation comes round it goes
      // straight to the drop -- **skipping animation 1**, which has already
      // played -- and that is the frame state 115 is waiting for.
      barrier.barrierCued1a0 = 1;
      barrier.barrierRaised19c = 1;
      if ((barrier.flags06 & 1u) != 0)
      {
        FUN_00225bc8_set_animation(barrier, 2);
      }
    }
    else if (animation == 2)
    {
      // The drop. Its cue keys once, gated on the same latch the rise set, and
      // the entity hides itself when the animation ends.
      if (barrier.barrierRaised19c == 1)
      {
        barrier.barrierCued1a0 = 1;
        if (environment.FUN_00267d38_playSound)
        {
          if (barrier.typeId00 == 0x143)
          {
            environment.FUN_00267d38_playSound(0xD6, barrier);
          }
          else if (barrier.typeId00 == 0x127)
          {
            environment.FUN_00267d38_playSound(0xD4, barrier);
          }
          else if (barrier.typeId00 == 0x144)
          {
            environment.FUN_00267d38_playSound(0xD2, barrier);
          }
        }
        barrier.barrierRaised19c = 0;
      }
      if ((barrier.flags06 & 1u) != 0)
      {
        barrier.flags06 = static_cast<std::uint16_t>(barrier.flags06 | 0x10u);
        barrier.halfword08 = static_cast<std::uint16_t>(barrier.halfword08 | 1u);
      }
    }

    // :183-190. +0x60 is 1 for one frame, written by state 115 when an incoming
    // attack's element *matched* the shield. The original then tail-calls
    // 0x002DDC68 -- the shatter burst, another LAB with no src/ file -- with
    // (self, 0, +0x198).
    //
    // **Not ported**, and unreachable in s14_e031: the demo has nothing
    // attacking the caster, so state 115 never writes the 1. The latch is still
    // consumed here so a future hit does not leave it set.
    if (barrier.state60 == 1)
    {
      barrier.state60 = 0;
    }
    (void)slot;
  }
  // FUN_002dee08 (0x002dee08), the behaviour of type 0x15C -- the ground disc
  // the launch lays down, and the spark it plants on each victim. Both are the
  // same entity; only the scale differs.
  //
  // It lives 32 frames and spins: +0x5C advances 0.349 radians (20 degrees) per
  // tick, and every frame it coin-flips between clearing its bone-0 override
  // and setting one at 0.5236 radians (30 degrees). The four bytes it zeroes at
  // the end are the hit bookkeeping -- it is not supposed to react to anything
  // it touches.
  void FUN_002dee08_lightning_disc(OriginalEntity &disc,
                                   std::size_t slot,
                                   const ActorEnvironment &environment)
  {
    disc.halfword08 = static_cast<std::uint16_t>(disc.halfword08 | 0x4000u);
    disc.lightningTimer1b0 = FUN_00248e58_step_timer(
        disc.lightningTimer1b0, static_cast<std::uint16_t>(environment.frameTicks));
    if (disc.lightningTimer1b0 == 0)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
      return;
    }

    disc.facingRadians5c += 0.34906578f; // fGpffffa9a8
    const std::uint32_t roll = environment.random ? environment.random() : 0;
    if (slot < environment.boneOverrides.size())
    {
      auto &overrides = environment.boneOverrides[slot];
      if ((roll & 1u) == 0)
      {
        orphen::ported::model::FUN_0020d9c8_clear_bone_override(overrides, 0);
      }
      else
      {
        // FUN_0030bfac(pose, 0, 0x1c) then pose[0] = uGpffffa9ac: rotation x
        // only, everything else zero, over two frames.
        std::array<float, orphen::ported::model::kPoseFieldCount> pose{};
        pose[0] = 0.5235987f;
        orphen::ported::model::FUN_0020d8c0_set_bone_override(overrides, 0, pose, 2);
      }
    }
    disc.freezeTimerBd = 0;
    disc.hitSourceC0 = 0;
    disc.hitFlagsC2 = 0;
    disc.pendingDamageBe = 0;
  }

  // FUN_002e4c00 (0x002e4c00), the behaviour of type 0x178 -- the flash. Not in
  // src/ and not defined in Ghidra; recovered from SLUS_200.11, where it is
  // four instructions:
  //
  //     lhu   v0, 6(a0)
  //     andi  v0, v0, 1
  //     beql  v0, zero, +
  //     j     FUN_00265ec0
  //
  // A pure one-shot: play the animation the launch set, destroy on the
  // animation-finished flag. Its 32-frame +0x1B0 timer is never read.
  void FUN_002e4c00_lightning_flash(OriginalEntity &flash,
                                    std::size_t slot,
                                    const ActorEnvironment &environment)
  {
    if ((flash.flags06 & 1u) != 0)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
    }
  }

  // FUN_002db230 (0x002db230), the behaviour of type 0x173 -- the burst a
  // fireball turns into when it hits something. Not in src/ and, unlike
  // FUN_002e4c00, not even a defined function in Ghidra; recovered from
  // SLUS_200.11, where it is ten instructions:
  //
  //     lhu   v0, 8(a0)
  //     lhu   v1, 6(a0)
  //     ori   v0, v0, 0x4000
  //     andi  v1, v1, 1
  //     beqz  v1, +
  //     sh    v0, 8(a0)        <- delay slot, so the store always happens
  //     j     FUN_00265ec0
  //
  // The same one-shot shape as the lightning flash, plus the +0x08 bit 0x4000
  // the fireball also carries. Without it nothing destroys the burst: it has no
  // timer of its own, so the explosion stood at the point of impact for the
  // rest of the scene.
  void FUN_002db230_fireball_burst(OriginalEntity &burst,
                                   std::size_t slot,
                                   const ActorEnvironment &environment)
  {
    burst.halfword08 = static_cast<std::uint16_t>(burst.halfword08 | 0x4000u);
    if ((burst.flags06 & 1u) != 0)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
    }
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
    case 0x002D73E8u: // FUN_002d73e8, type 0x192, the target cursor
    case 0x002D9C88u: // FUN_002d9c88, type 0x18F, the ground ring
    case 0x002DEAE8u: // FUN_002deae8, type 0x174, Bite of Lightning's hand effect
    case 0x002E3110u: // FUN_002e3110, type 0x175, Falcon of Death's hand effect
    case 0x002E2048u: // FUN_002e2048, type 0x177, Hammer of Evil's hand effect
    case 0x002DFD38u: // FUN_002dfd38, type 0x179, Pinnacle of the Sun's hand effect
    case 0x002E0F80u: // FUN_002e0f80, type 0x17C, Hail of Heavens' hand effect
    case 0x002E3490u: // 0x002e3490, type 0x15D, Falcon of Death's projectile
    case 0x002E23C8u: // 0x002e23c8, type 0x156, Hammer of Evil's projectile
    case 0x002E00B8u: // 0x002e00b8, type 0x158, Pinnacle of the Sun's projectile
    case 0x002E1300u: // 0x002e1300, type 0x15A, Hail of Heavens' projectile
    case 0x002E9628u: // 0x002e9628, type 0x1D9, Falcon of Death's arrival flash
    case 0x002DE0A8u: // LAB_002de0a8, type 0x143, Shield of Immunity
    case 0x002DE0B0u: // LAB_002de0b0, type 0x127, Shield of Inferno
    case 0x002DE0B8u: // LAB_002de0b8, type 0x144, Armor of Purity
    case 0x002DB548u: // FUN_002db548, type 0x13A, Bolt of Thunder's hand effect
    case 0x002DBF48u: // FUN_002dbf48, type 0x13B, Feathers of the Hurricane's hand
    case 0x002DC960u: // FUN_002dc960, type 0x13C, Smoke of Pain's hand
    case 0x002DD358u: // FUN_002dd358, type 0x194, Coldness of Destruction's hand
    case 0x002DB7D0u: // FUN_002db7d0, type 0x155, Bolt of Thunder's shot
    case 0x002DC1C8u: // FUN_002dc1c8, type 0x157, Feathers' shot
    case 0x002DCC20u: // FUN_002dcc20, type 0x159, Smoke of Pain's shot
    case 0x002DD618u: // FUN_002dd618, type 0x195, Coldness' shot
    case 0x002DBC60u: // 0x002dbc60, type 0x170 -- `j 0x2db230`, the shared burst
    case 0x002DC680u: // 0x002dc680, type 0x171, the same
    case 0x002DD070u: // 0x002dd070, type 0x172, the same
    case 0x002DDC60u: // 0x002ddc60, type 0x196, the same
    case 0x002DF018u: // FUN_002df018, type 0x13E, Bite of Lightning's summon
    case 0x002E34B8u: // FUN_002e34b8, type 0x13F, Falcon of Death's summon
    case 0x002E23E8u: // FUN_002e23e8, type 0x140, Hammer of Evil's summon
    case 0x002E01F8u: // FUN_002e01f8, type 0x141, Pinnacle of the Sun's summon
    case 0x002E1320u: // FUN_002e1320, type 0x142, Hail of Heavens' summon
    case 0x002DEE08u: // FUN_002dee08, type 0x15C, its ground disc and victim sparks
    case 0x002E4C00u: // FUN_002e4c00, type 0x178, its one-shot flash
    case 0x002DB230u: // FUN_002db230, type 0x173, the fireball's impact burst
    case 0x00279298u: // FUN_00279298, type 0x7F, the giant crab
    case 0x00299390u: // FUN_00299390, type 0x95, the s14_e002 mast boss
    case 0x002EB180u: // FUN_002eb180, type 0x10D, the water splash
    case 0x002EA7F0u: // FUN_002ea7f0, type 0x10C, the stamp's bubbles
    case 0x002EA238u: // FUN_002ea238, type 0x10B, the crab's boulder
    case 0x002EB680u: // FUN_002eb680, types 0xAE and 0xAF, the crab's shed claws
    case 0x00276C30u: // FUN_00276c30, type 0x7E, the crab's swarm
    case 0x0027F288u: // FUN_0027f288, type 0x80, a battle enemy
    case 0x0028A958u: // FUN_0028a958, type 0x8A, a battle enemy
    case 0x0028B848u: // FUN_0028b848, type 0x8B, the s14_e031 target dummy
    case 0x002D0EA8u: // FUN_002d0ea8, type 0x58, the field HP gauge
    case 0x002D5748u: // 0x002d5748, type 0x68, the health bar
    case 0x002EB990u: // FUN_002eb990, type 0x10E, the flyer's shot
    case 0x002EBC30u: // FUN_002ebc30, type 0x10F, the swoop's dust
    case 0x002ECB08u: // FUN_002ecb08, type 0x113, the Maneater's spores
    case 0x002ED3E0u: // FUN_002ed3e0, type 0x1AA, the burning ship
    case 0x002ED980u: // 0x002ED980,   type 0x1AB, one puff of its smoke
    case 0x002ED9A0u: // FUN_002ed9a0, type 0x1AC, one link of its fire ring
    case 0x002EDC40u: // FUN_002edc40, type 0x1AE, the mast creature's wash
    case 0x002D8CE0u: // FUN_002d8ce0, type 0x118, the status aura
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
    case 0x002D73E8u:
      return "FUN_002d73e8 (target cursor)";
    case 0x002D0EA8u:
      return "FUN_002d0ea8 (field HP gauge)";
    case 0x002D5748u:
      return "LAB_002d5748 (health bar)";
    case 0x002EB990u:
      return "FUN_002eb990 (enemy shot)";
    case 0x002EBC30u:
      return "FUN_002ebc30 (swoop dust)";
    case 0x002ECB08u:
      return "FUN_002ecb08 (poison spore)";
    case 0x002ED3E0u:
      return "FUN_002ed3e0 (ship fire)";
    case 0x002ED980u:
      return "LAB_002ed980 (ship smoke puff)";
    case 0x002ED9A0u:
      return "FUN_002ed9a0 (ship fire ring)";
    case 0x002EDC40u:
      return "FUN_002edc40 (mast wash)";
    case 0x002D8CE0u:
      return "FUN_002d8ce0 (status aura)";
    case 0x002D9C88u:
      return "FUN_002d9c88 (cast marker)";
    case 0x002DEAE8u:
      return "FUN_002deae8 (bite of lightning)";
    case 0x002E3110u:
      return "FUN_002e3110 (falcon hand 0x175)";
    case 0x002E2048u:
      return "FUN_002e2048 (hammer hand 0x177)";
    case 0x002DFD38u:
      return "FUN_002dfd38 (pinnacle hand 0x179)";
    case 0x002E0F80u:
      return "FUN_002e0f80 (hail hand 0x17c)";
    case 0x002E3490u:
      return "LAB_002e3490 (falcon projectile 0x15d)";
    case 0x002E23C8u:
      return "LAB_002e23c8 (hammer projectile 0x156)";
    case 0x002E00B8u:
      return "LAB_002e00b8 (pinnacle projectile 0x158)";
    case 0x002E1300u:
      return "LAB_002e1300 (hail projectile 0x15a)";
    case 0x002E9628u:
      return "0x002e9628 (Falcon arrival flash)";
    case 0x002DE0A8u:
      return "LAB_002de0b8 (shield of immunity 0x143)";
    case 0x002DE0B0u:
      return "LAB_002de0b8 (shield of inferno 0x127)";
    case 0x002DE0B8u:
      return "LAB_002de0b8 (armor of purity 0x144)";
    case 0x002DB548u:
      return "FUN_002db548 (bolt of thunder hand 0x13a)";
    case 0x002DBF48u:
      return "FUN_002dbf48 (feathers hand 0x13b)";
    case 0x002DC960u:
      return "FUN_002dc960 (smoke of pain hand 0x13c)";
    case 0x002DD358u:
      return "FUN_002dd358 (coldness hand 0x194)";
    case 0x002DB7D0u:
      return "FUN_002db7d0 (bolt of thunder shot 0x155)";
    case 0x002DC1C8u:
      return "FUN_002dc1c8 (feathers shot 0x157)";
    case 0x002DCC20u:
      return "FUN_002dcc20 (smoke of pain shot 0x159)";
    case 0x002DD618u:
      return "FUN_002dd618 (coldness shot 0x195)";
    case 0x002DBC60u:
    case 0x002DC680u:
    case 0x002DD070u:
    case 0x002DDC60u:
      return "FUN_002db230 (elemental burst)";
    case 0x002DF018u:
      return "FUN_002df018 (Bite of Lightning summon)";
    case 0x002E34B8u:
      return "FUN_002e34b8 (Falcon of Death summon)";
    case 0x002E23E8u:
      return "FUN_002e23e8 (Hammer of Evil summon)";
    case 0x002E01F8u:
      return "FUN_002e01f8 (Pinnacle of the Sun summon)";
    case 0x002E1320u:
      return "FUN_002e1320 (Hail of Heavens summon)";
    case 0x002DEE08u:
      return "FUN_002dee08 (lightning disc)";
    case 0x002E4C00u:
      return "FUN_002e4c00 (lightning flash)";
    case 0x002DB230u:
      return "FUN_002db230 (fireball burst)";
    case 0x00279298u:
      return "FUN_00279298 (crab boss 0x7f)";
    case 0x00299390u:
      return "FUN_00299390 (mast boss 0x95)";
    case 0x002EB180u:
      return "FUN_002eb180 (water splash 0x10d)";
    case 0x002EA7F0u:
      return "FUN_002ea7f0 (stamp bubble 0x10c)";
    case 0x002EA238u:
      return "FUN_002ea238 (crab boulder 0x10b)";
    case 0x002EB680u:
      return "FUN_002eb680 (crab claw 0xae/0xaf)";
    case 0x00276C30u:
      return "FUN_00276c30 (swarm crab 0x7e)";
    case 0x0027F288u:
      return "FUN_0027f288 (battle enemy 0x80)";
    case 0x0028A958u:
      return "FUN_0028a958 (battle enemy 0x8a)";
    case 0x0028B848u:
      return "FUN_0028b848 (target dummy 0x8b)";
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
      case 0x002D73E8u:
        FUN_002d73e8_target_cursor(entity, slot, slotEnvironment);
        break;
      case 0x002D5748u:
        FUN_002d5748_health_bar(entity, environment.frameTicks);
        break;
      case 0x002D0EA8u:
      {
        // FUN_002D0EA8 reads the lead player outright -- its $s1 is the literal
        // 0x0058BEB0 -- rather than anything the walk hands it.
        FieldHpGaugeEnvironment gaugeEnvironment;
        gaugeEnvironment.frameTicks = environment.frameTicks;
        gaugeEnvironment.DAT_00354d2c_gameMode = environment.DAT_00354d2c_gameMode;
        gaugeEnvironment.DAT_00355054_letterboxMode = environment.DAT_00355054_letterboxMode;
        gaugeEnvironment.DAT_003551ec_sceneRequest = environment.DAT_003551ec_sceneRequest;
        gaugeEnvironment.DAT_0034ab70_flag50b =
            environment.eventFlag ? environment.eventFlag(0x50B) : false;
        FUN_002d0ea8_field_hp_gauge(entity, pool.leadPlayer(), gaugeEnvironment);
        break;
      }
      case 0x002EB990u:
        FUN_002eb990_enemy_shot(entity, slot, environment);
        break;
      case 0x002EBC30u:
        FUN_002ebc30_swoop_puff(entity, slot, environment);
        break;
      case 0x002ECB08u:
        FUN_002ecb08_spore(entity, slot, environment);
        break;
      // Types 0x1AA / 0x1AB / 0x1AC, the burning ship in s01_e013's animatic.
      // The middle one has no src/ file because it is eight instructions; the
      // disassembly is in original_ship_fire.h.
      case 0x002ED3E0u:
        FUN_002ed3e0_ship_fire(entity, slot, environment);
        break;
      case 0x002ED980u:
        LAB_002ed980_ship_fire_puff(entity, slot, environment);
        break;
      case 0x002ED9A0u:
        FUN_002ed9a0_ship_fire_ring(entity, slot, environment);
        break;
      case 0x002EDC40u:
        FUN_002edc40_mast_wash_entry(entity, slot, environment);
        break;
      case 0x002D8CE0u:
        FUN_002d8ce0_status_aura(entity, slot, environment);
        break;
      case 0x002D9C88u:
        FUN_002d9c88_cast_marker(entity, slot, slotEnvironment);
        break;
      case 0x002DEAE8u:
        FUN_002deae8_lightning_hand(entity, slot, slotEnvironment);
        break;
      case 0x002E3110u:
      case 0x002E2048u:
      case 0x002DFD38u:
      case 0x002E0F80u:
      {
        const ElementalSpellB *spell = elementalSpellBForHand(entity.typeId00);
        if (spell != nullptr)
        {
          FUN_002e3110_elemental_hand(entity, slotEnvironment, *spell);
        }
        break;
      }
      case 0x002E3490u:
        LAB_002e3490_elemental_projectile(entity, slot, slotEnvironment, true);
        break;
      case 0x002E23C8u:
      case 0x002E00B8u:
      case 0x002E1300u:
      // 0x002E9628, type 0x1D9: the same eight instructions again. It is the
      // flash Falcon of Death's spawner drops at the caster's feet, and it goes
      // away the frame its animation comes round like every other one.
      case 0x002E9628u:
        LAB_002e3490_elemental_projectile(entity, slot, slotEnvironment, false);
        break;
      case 0x002DE0A8u:
      case 0x002DE0B0u:
      case 0x002DE0B8u:
        LAB_002de0b8_shield_barrier(entity, slot, slotEnvironment);
        break;
      case 0x002DB548u:
      case 0x002DBF48u:
      case 0x002DC960u:
      case 0x002DD358u:
      {
        const Kind12Spell *spell = kind12ForHand(entity.typeId00);
        if (spell != nullptr)
        {
          FUN_002db548_kind12_hand(entity, slotEnvironment, *spell);
        }
        break;
      }
      case 0x002DB7D0u:
      case 0x002DC1C8u:
      case 0x002DCC20u:
      case 0x002DD618u:
      {
        const Kind12Spell *spell = kind12ForProjectile(entity.typeId00);
        if (spell != nullptr)
        {
          FUN_002db7d0_kind12_projectile(entity, slot, slotEnvironment, *spell);
        }
        break;
      }
      // Types 0x170, 0x171, 0x172 and 0x196 are two-instruction jumps into
      // 0x002DB230, so they run Hand of Pyro's burst unchanged.
      case 0x002DBC60u:
      case 0x002DC680u:
      case 0x002DD070u:
      case 0x002DDC60u:
        FUN_002db230_fireball_burst(entity, slot, slotEnvironment);
        break;
      case 0x002DF018u:
        FUN_002df018_bite_summon(entity, slot, slotEnvironment);
        break;
      case 0x002E34B8u:
      case 0x002E23E8u:
      case 0x002E01F8u:
      case 0x002E1320u:
        FUN_002e01f8_summon(entity, slot, slotEnvironment);
        break;
      case 0x002DEE08u:
        FUN_002dee08_lightning_disc(entity, slot, slotEnvironment);
        break;
      case 0x002E4C00u:
        FUN_002e4c00_lightning_flash(entity, slot, slotEnvironment);
        break;
      case 0x002DB230u:
        FUN_002db230_fireball_burst(entity, slot, slotEnvironment);
        break;
      case 0x00279298u:
        FUN_00279298_crab_boss(entity, slot, slotEnvironment, trace);
        break;
      case 0x00299390u:
        FUN_00299390_mast_boss(entity, slot, slotEnvironment, trace);
        break;
      case 0x002EB180u:
        FUN_002eb180_water_splash(entity, slot, slotEnvironment);
        break;
      case 0x002EA7F0u:
        FUN_002ea7f0_bubble(entity, slot, slotEnvironment);
        break;
      case 0x002EA238u:
        FUN_002ea238_thrown_rock(entity, slot, slotEnvironment);
        break;
      case 0x002EB680u:
        FUN_002eb680_thrown_claw(entity, slot, slotEnvironment);
        break;
      case 0x00276C30u:
        FUN_00276c30_swarm_crab(entity, slot, slotEnvironment, trace);
        break;
      case 0x0027F288u:
        FUN_0027f288_enemy80(entity, slot, slotEnvironment, trace);
        break;
      case 0x0028A958u:
        FUN_0028a958_enemy8a(entity, slot, slotEnvironment, trace);
        break;
      case 0x0028B848u:
        FUN_0028b848_enemy8b(entity, slot, slotEnvironment, trace);
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
