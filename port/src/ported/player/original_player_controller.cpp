#include "ported/player/original_player_controller.h"

#include "ported/entity/actor_frame_update.h"
#include "ported/entity/entity_collision.h"
#include "ported/entity/original_hit_test.h"
#include "ported/entity/original_party_weapons.h"
#include "ported/original_frame_timing.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace orphen::ported::player
{
  namespace
  {

    constexpr float kOriginalRunStepPerFrame = 0.0450000018f;   // fGpffff8a4c.
    constexpr float kOriginalWalkStepPerFrame = 0.0230000000f;  // fGpffff8a50.
    constexpr float kOriginalRunStickThreshold = 100.0f;        // FUN_00256bb8.
    constexpr float kOriginalFallHeight = 0.370000005f;         // fGpffff8a48.
    constexpr float kOriginal0x1dTurnRate = 0.069813155f;       // fGpffff8a40, 4 deg; camera sub-mode 0x1D only.
    constexpr float kOriginalAirControlUnit = 1.22070314e-05f;  // DAT_00352878.
    constexpr float kOriginalFullStickMagnitude = 128.0f;       // DAT_003555e8 at full deflection.
    constexpr float kOriginalJumpVelocity = 0.0529999994f;      // DAT_0035287c/DAT_00355000.
    constexpr float kOriginalGravity = 0.000750000007f;         // JUMP TEST G_FORCE 00075 at x100000 scale.
    // DAT_0035246C / DAT_00352470: the landing ring sits 0.15 below the feet and
    // its puffs are 0.4 across. FUN_002262C0 passes the actor's own +0x54 for
    // both the x jitter and the ring radius, with no y jitter.
    constexpr float kDAT_0035246c_dustDrop = 0.15f;
    constexpr float kMovementEpsilon = 0.0001f;
    constexpr std::uint32_t kPhysicsFlagGrounded = 0x0001;

    // FUN_00225c90's latches on entity +0x06, the same three the animation
    // stepper writes in ported/model/entity_animation.cpp.
    //   0x01  the last timeline entry finished -- the animation is over
    //   0x04  the current entry's duration ran out this frame
    //   0x08  a new entry was taken this frame
    constexpr std::uint16_t kAnimationComplete06 = 0x0001;
    constexpr std::uint16_t kAnimationExpired06 = 0x0004;
    constexpr std::uint16_t kAnimationStepped06 = 0x0008;

    // FUN_00256130's test on the current keyframe's third halfword (+0xAA).
    // The animation's own data is what says when the blade should stop: the
    // keyframe carrying this bit is the end of the slash.
    constexpr std::uint16_t kKeyframeEventSwordEnd = 0x0200;

    // FUN_00257b70 is one call, FUN_00267d38(0xA4, entity) -- the swing.
    // FUN_00251ED8's hit-reaction constants, all gp-relative against
    // gp = 0x00359F70 and read out of SLUS_200.11 rather than guessed.
    constexpr float kfGpffff88c4_pi = 3.141592025756836f;       // 0x00352834
    constexpr float kfGpffff88cc_pi = 3.141592025756836f;       // 0x0035283C
    constexpr float kuGpffff88c0_deathKnockback = 0.00312500005f; // 0x00352830
    constexpr float kuGpffff88c8_deathPopUp = 0.0450000018f;    // 0x00352838
    constexpr float kuGpffff88d0_knockbackSpeed = 0.00312500005f; // 0x00352840
    constexpr float kuGpffff88d4_knockbackPopUp = 0.0350000001f; // 0x00352844
    // FUN_00251ED8:60-71's terrain-hazard entry: an airborne lead only drowns
    // once it is falling faster than this.
    constexpr float kfGpffff88bc_hazardFallSpeed = -0.100000001f; // 0x0035282C

    constexpr std::uint16_t kSoundCueSwordSwing = 0xa4;
    // FUN_00257b50 and FUN_00257b40, the two halves of the cast: 0xA5 when the
    // state starts and 0xA6 when the projectile leaves the hand.
    constexpr std::uint16_t kSoundCueMagicCast = 0xa5;
    constexpr std::uint16_t kSoundCueMagicLaunch = 0xa6;

    // The other characters' action states, PTR_FUN_0031e160[2..7]. 0x22 is
    // 0x002569D0, a bare `jr ra`, and nothing ever enters it.
    constexpr std::uint16_t kStateClass3Attack = 0x1e; // FUN_002563e8
    constexpr std::uint16_t kStateClass3Magic = 0x1f;  // FUN_00256548
    constexpr std::uint16_t kStateClass4Attack = 0x20; // FUN_00256620
    constexpr std::uint16_t kStateClass4Magic = 0x21;  // FUN_002567c0
    constexpr std::uint16_t kStateUnused22 = 0x22;     // 0x002569d0
    constexpr std::uint16_t kStateClass5Magic = 0x23;  // FUN_002569d8

    // FUN_00257b60 / b70 / b30 / b20: one FUN_00267d38 each.
    constexpr std::uint16_t kSoundCueClass3Swing = 0xe5;
    constexpr std::uint16_t kSoundCueClass4Swing = 0xa4;
    constexpr std::uint16_t kSoundCueClass4Throw = 0xeb;
    constexpr std::uint16_t kSoundCueClass5Cast = 0xa7;

    // FUN_00256548's orb spawn point: DAT_0035299c off +0x24 and DAT_003529a0
    // up from +0x28. The first really is the ground-plane axis, not the height.
    constexpr float kDAT_0035299c_orbBack = 0.200000003f;
    constexpr float kDAT_003529a0_orbUp = 0.800000011f;

    // FUN_00256bb8's idle block: a fidget in 0x2F is never cut short.
    constexpr std::uint16_t kAnimationHeldIdle = 0x2f;
    constexpr std::uint16_t kAnimationClass3Fidget = 0x6a;
    constexpr std::uint16_t kKeyframeEvent100 = 0x0100;

    float horizontalMagnitude(const orphen::ported::psm2::Vec3 &value)
    {
      return std::sqrt(value.x * value.x + value.y * value.y);
    }

    bool hasMovementInput(const orphen::ported::psm2::Vec3 &value)
    {
      return horizontalMagnitude(value) > kMovementEpsilon;
    }

    float wrapAngle(float radians)
    {
      constexpr float kPi = 3.14159265359f;
      constexpr float kTwoPi = 6.28318530718f;
      while (radians > kPi)
      {
        radians -= kTwoPi;
      }
      while (radians < -kPi)
      {
        radians += kTwoPi;
      }
      return radians;
    }

    // FUN_002166e8.
    float shortestAngleDelta(float from, float to)
    {
      return wrapAngle(to - from);
    }

    // FUN_002262c0:0x00226cb4. The gate on an upward step is `fVar18 - fVar19 <
    // DAT_00352434`, and it is checked twice -- once before the provisional
    // raise and once on the re-query afterwards. DAT_00352434 is **0.26**, a
    // global, and it is *strictly* less than.
    //
    // Entity +0x80 is not this. The original's only use of it is
    // `if ((float)puVar11[2] <= *(float *)(iVar12 + 0x80))` on the same path,
    // where puVar11[2] is the workspace's copy of the destination surface's
    // stored slope angle -- and type 1's descriptor value is 0.8727, which is 50
    // degrees in radians. It is the walkable-slope limit. Using it as a step
    // height let the lead ratchet 0.75 up per move and climb out of the hold.
    constexpr float kStepHeightDAT_00352434 = 0.26f;

    // The rest of FUN_002262c0's constant block, read out of SLUS_200.11.
    constexpr float kDAT_00352428_velocityFloor = -1.0e-5f; // v == 0 after gravity becomes this
    constexpr float kDAT_00352430_holdLift = 0.02f;         // +0x38 on the LAB_00226988 hold
    constexpr float kDAT_00352438_stepLift = 0.063f;        // the provisional raise past a step
    constexpr float kDAT_0035243c_firstRetryScale = 0.3f;
    constexpr float kDAT_00352440_narrowScale = 0.7f;
    constexpr float kDAT_00352444_narrowTurn = 0.3490658f;   // 20 degrees
    constexpr float kDAT_00352448_narrowTurnBack = 0.3490658f;
    constexpr float kDAT_0035244c_wideTurn = 1.0471973f;     // 60 degrees
    constexpr float kDAT_00352450_wideTurnBack = 1.0471973f;
    constexpr float kDAT_00352454_easeWindow = 0.26f;        // +0x2C eases up steps smaller than this
    constexpr float kDAT_00352458_easeStep = 0.04f;

  } // namespace

  void OriginalPlayerController::resetAtOrigin(const OriginalTerrainSampler &terrainSampler)
  {
    resetAt({}, terrainSampler);
  }

  void OriginalPlayerController::resetAt(const orphen::ported::psm2::Vec3 &spawn,
                                         const OriginalTerrainSampler &terrainSampler)
  {
    entity() = orphen::ported::entity::OriginalEntity{};
    // The lead player is type id 1. Confirmed from an EE dump, where pool slot 0
    // reads type 0x001, and consistent with the radius/height defaults below
    // coming from type 1's descriptor. It had been left at 0, which reads as
    // "empty slot" to anything that inspects the pool -- type 0x62's chase state
    // refuses a target whose type is 0, so the enemies ignored the player.
    entity().typeId00 = 1;
    entity().positionX20 = spawn.x;
    entity().positionZ24 = spawn.y;
    entity().positionY28 = spawn.z;
    entity().verticalAcceleration48 = kOriginalGravity;
    // radius54/height58 come from OriginalEntity's own defaults (type id 1's
    // descriptor), and so does slopeLimit80: +0x80 is the walkable-slope limit
    // and type 1's descriptor value is 0.872665 = 50 degrees. The port used to
    // put 0.75 here and read the field as a step height, which let the lead
    // ratchet three quarters of a unit up per move and climb out of the hold.
    entity().slopeLimit80 = 0.872664626f;
    // FUN_00229c40 seeds +0x7C to 100.0; every save-state dump has the lead on it.
    entity().maxStepDown7c = 100.0f;
    FUN_00252d88_return_to_idle_state();

    if (terrainSampler)
    {
      const auto groundSample = terrainSampler(entity().positionX20,
                                               entity().positionZ24,
                                               entity().positionY28,
                                               terrainQueryForEntity(entity().positionY28));
      if (groundSample.has_value())
      {
        entity().groundHeight4c = groundSample->height;
        entity().previousGroundHeight50 = groundSample->height;
        entity().positionY28 = groundSample->height;
        entity().previousY2c = groundSample->height;
        entity().collisionFlags0c = kPhysicsFlagGrounded;
      }
      // FUN_0022A418:219 is `+0x4C = FUN_00227070(x, y, lead)`, and FUN_00227070
      // leaves the surface's words in +0x6C / +0x70 as well. A lead that is
      // placed and never moves reads them from here and nowhere else: the
      // physics only queries the ground for a horizontal move.
      if (const auto surface = FUN_00227390_validate_destination(entity().positionX20,
                                                                 entity().positionZ24,
                                                                 entity().positionY28,
                                                                 terrainSampler))
      {
        entity().flagWord6c = surface->terrainFlags;
        entity().flagWord70 = surface->terrainFlags;
        entity().groundPrimitive0a = surface->packedPrimitive;
      }
    }
  }

  void OriginalPlayerController::update(std::uint32_t frameTicks,
                                        const OriginalPlayerFrameInput &input,
                                        const OriginalTerrainSampler &terrainSampler,
                                        const OriginalInteractionProbe &interactionProbe)
  {
    // FUN_002000c0 clamps DAT_003555bc to [0x20, 0x80] before anything reads it.
    const std::uint32_t clampedFrameTicks =
        std::clamp(frameTicks, orphen::ported::kMinFrameTicks, orphen::ported::kMaxFrameTicks);

    // FUN_00251ED8:21-23, the first thing it does after the null check:
    //
    //   uGpffffbd54 = heldMapped & 0x20;
    //   if (cGpffffb66a == 0) uGpffffbd54 = 0;
    //
    // 0x20 is the attack button and cGpffffb66a is the debug byte, so this is
    // the arm half of the moon jump. FUN_002534D8 is the only thing that reads
    // it.
    uGpffffbd54_moonJumpArmed_ =
        input.cGpffffb66a_debugActive
            ? (input.uGpffffb688_heldThisFrame & kOriginalMappedActionAttack)
            : 0u;
    // **The movement request is not cleared here.** FUN_00251ed8 does not touch
    // +0x30 / +0x34 / +0x38 anywhere -- its only write to them is the *additive*
    // leader-follow at its tail (`psVar8[0x18] += ...`, halfword indices, i.e.
    // byte +0x30). FUN_002262c0's epilogue is the sole owner of the clear, and
    // it clears only after it has spent them. FUN_00253080, the drift pass, does
    // assign them, but only on a 0xD-class surface or while airborne with
    // residual drift; on ordinary ground it leaves them alone.
    //
    // Clearing them here destroyed every movement a *script* asked of the lead.
    // FUN_002239c8 runs FUN_0025b778 before FUN_00251ed8, so opcodes
    // 0xEE..0xF1 -- which accumulate into +0x30 / +0x34 on the focus entity and
    // wait for it to arrive -- wrote their request one statement before this
    // wiped it. Focused on pool slot 0 the actor never moved, never reached its
    // target, never advanced +0x1BC and never set the event flag its cutscene
    // gates on. This is the same bug the actor loop had (see the FUN_00239ce0
    // note in port/README.md); the lead's copy of it survived that fix.

    // FUN_00251ED8:60-71, the terrain-hazard entry. A surface whose terrain
    // word carries 0x1000000 -- water, lava, a pit -- takes the player into
    // state 0x19 once it is standing on it or falling onto it. A class-1
    // surface (top nibble 1) is the exception: it only counts below the water
    // line DAT_003556FC. +0x04 |= 0x110 switches the physics off (0x100) so the
    // handler can sink the body by hand; +0x134 = 0x7C is the fade it sinks
    // through; the camera sub-mode goes into +0x1B4 and is parked at 0xFF so the
    // view holds, and FUN_00255E40 restores both.
    {
      const std::uint32_t terrain = entity().flagWord6c;
      const bool settledOrFalling = (entity().collisionFlags0c & kPhysicsFlagGrounded) != 0 ||
                                    entity().verticalVelocity44 < kfGpffff88bc_hazardFallSpeed;
      const float waterLine =
          terrainHazard_.DAT_003556fc_waterLine ? terrainHazard_.DAT_003556fc_waterLine() : 0.0f;
      if ((terrain & 0x01000000u) != 0 && entity().state60 != kStateTerrainHazard &&
          settledOrFalling &&
          ((terrain & 0xF0000000u) != 0x10000000u || entity().positionY28 <= waterLine))
      {
        FUN_00225bf0_set_entity_state(kStateTerrainHazard, kAnimationTerrainHazard);
        entity().fadeLevel134 = 0x7C;
        entity().halfword04 = static_cast<std::uint16_t>(entity().halfword04 | 0x110u);
        entity().halfword08 = static_cast<std::uint16_t>(entity().halfword08 & 0xFFFBu);
        entity().fadeRamp62 = 0;
        // +0x1B4 is the climb's face halfword too; the two never overlap.
        const std::uint8_t subMode =
            terrainHazard_.cameraSubMode ? terrainHazard_.cameraSubMode() : 0;
        entity().climbFace1b4 = static_cast<std::int16_t>(static_cast<std::int8_t>(subMode));
        if (terrainHazard_.setCameraSubMode)
        {
          terrainHazard_.setCameraSubMode(0xFF);
        }
      }
    }

    // FUN_00251ED8:97-230, ahead of the dispatch: the +0xBE mailbox. It can
    // replace the state outright, so the branch below sees the reaction the hit
    // just installed rather than the state the player was in.
    FUN_00251ed8_apply_pending_damage(clampedFrameTicks);

    // FUN_00251ed8's table dispatch is exclusive: exactly one handler runs.
    // A state owned elsewhere -- the chest cutscene, states 0x0C..0x15 --
    // takes the frame and the field branches below do not see it.
    const bool handledElsewhere = scriptedStateStep_ && scriptedStateStep_(clampedFrameTicks);

    if (handledElsewhere)
    {
      // Physics still runs after the handler, exactly as FUN_00251ed8 falls
      // through to FUN_00253080. The cutscene states write positions directly
      // and leave the movement request at zero, so this only re-settles them
      // onto the floor.
    }
    else if (entity().state60 == kStateScriptDriven)
    {
      // PTR_FUN_0031e0e8[10] is 0x00254cf0, which is `jr ra; nop` -- a real
      // no-op, and the whole of how a cutscene takes the controller away. The
      // player reads no input, requests no movement and changes no state; the
      // scene's lead-bound script slot drives it instead. Opcode 0x6D puts the
      // lead here and 0xA8 does too.
      //
      // Falling through to the grounded field branch, which is what used to
      // happen, handed control straight back to the pad the moment a cutscene
      // asked for it.
    }
    else if (entity().state60 == 2)
    {
      FUN_002534d8_update_airborne_state(clampedFrameTicks, input);
    }
    else if (entity().state60 == kStateClimbHold)
    {
      FUN_002537a0_update_climb_hold(clampedFrameTicks, input);
    }
    else if (entity().state60 == kStateClimbMove)
    {
      FUN_00253be8_update_climb_move(clampedFrameTicks, input);
    }
    else if (entity().state60 == kStateClimbOver)
    {
      FUN_002540d0_update_climb_over(clampedFrameTicks);
    }
    else if (entity().state60 == kStateSwordAttack)
    {
      // FUN_00251ed8 sends states >= 0x1C through PTR_FUN_0031e160, whose first
      // entry is FUN_00256130. Without this the swing fell back into the
      // grounded field branch, which would re-read the pad and overwrite the
      // attack animation with `stand` on the very next frame.
      FUN_00256130_update_sword_attack();
    }
    else if (entity().state60 == kStateMagicCast)
    {
      FUN_002562b0_update_magic_cast();
    }
    else if (entity().state60 == kStateClass3Attack)
    {
      FUN_002563e8_update_class3_attack(clampedFrameTicks, input);
    }
    else if (entity().state60 == kStateClass3Magic)
    {
      FUN_00256548_update_class3_magic(clampedFrameTicks);
    }
    else if (entity().state60 == kStateClass4Attack)
    {
      FUN_00256620_update_class4_attack(clampedFrameTicks, input);
    }
    else if (entity().state60 == kStateClass4Magic)
    {
      FUN_002567c0_update_class4_magic(clampedFrameTicks);
    }
    else if (entity().state60 == kStateUnused22)
    {
    }
    else if (entity().state60 == kStateClass5Magic)
    {
      FUN_002569d8_update_class5_magic(clampedFrameTicks, input);
    }
    else if (entity().state60 == kStateHitStagger)
    {
      FUN_002554d8_update_hit_stagger(clampedFrameTicks);
    }
    else if (entity().state60 == kStateHitFlatten)
    {
      FUN_002555a8_update_hit_flatten();
    }
    else if (entity().state60 == kStateHitKnockback)
    {
      FUN_002555d8_update_hit_knockback(clampedFrameTicks);
    }
    else if (entity().state60 == kStateTerrainHazard)
    {
      FUN_002557a0_update_terrain_hazard(clampedFrameTicks);
    }
    else if (entity().state60 == kStateGameOverEnter)
    {
      // FUN_00255820 stages the whole game over and leaves the entity in
      // 0x1B, so this branch runs exactly once.
      FUN_00255820_enter_game_over(entity(), gameOver_);
    }
    else if (entity().state60 == kStateGameOverHold)
    {
      FUN_002559e8_update_game_over(entity(), clampedFrameTicks, gameOver_);
    }
    else
    {
      // PTR_FUN_0031E0E8[0] and [1], FUN_002533C0 and FUN_00253430: both call
      // FUN_00256BB8 and then FUN_00252DE0, the climb, whatever the first one
      // did. The climb reads Cross too, and only an interaction clears it first.
      // States 6..9 also land here; they are not ported.
      const std::uint16_t entryState = entity().state60;
      const bool padCleared =
          FUN_00256bb8_update_grounded_field_state(clampedFrameTicks, input, interactionProbe);
      if ((entryState == 0 || entryState == 1) && !padCleared)
      {
        FUN_00252de0_start_climb(clampedFrameTicks, input);
      }
    }

    FUN_002262c0_integrate_physics(clampedFrameTicks, terrainSampler);

    // +0xA8 used to be advanced here, once per frame, as a substate counter.
    // It is not one: FUN_00225c90 owns that halfword and steps it by *two per
    // keyframe* of the current animation, and FUN_002534d8's `< 4` / `== 4`
    // jump-startup tests are against keyframes rather than frames. Advancing it
    // here as well double-counted it, and -- because script object register 6
    // reads the same halfword -- it was also being written by two owners at
    // once. The animation pass is the only writer now.
  }

  OriginalPlayerSnapshot OriginalPlayerController::snapshot() const
  {
    const bool grounded = (entity().collisionFlags0c & kPhysicsFlagGrounded) != 0;
    return {{entity().positionX20, entity().positionZ24, entity().positionY28},
            entity().facingRadians5c,
            entity().state60,
            entity().animationA0,
            entity().timelineCursorA8,
            entity().collisionFlags0c,
            entity().verticalVelocity44,
            entity().height58,
            grounded,
            entity().running,
            entity().state60 >= kStateClimbHold && entity().state60 <= kStateClimbOver ? entity().climbFace1b4
                                                                                      : std::int16_t{-1}};
  }

  std::optional<std::uint32_t> OriginalPlayerController::currentSurfaceTerrainFlags() const
  {
    if ((entity().collisionFlags0c & kPhysicsFlagGrounded) == 0)
    {
      return std::nullopt;
    }
    return entity().flagWord6c;
  }

  void OriginalPlayerController::FUN_00225bf0_set_entity_state(std::uint16_t state, std::uint16_t substate)
  {
    entity().state60 = state;
    entity().stateResetA4 = 999;
    entity().animationA0 = substate;
    entity().previousSubstateA2 = 0xffff;
    entity().flags06 &= 0xff38;
    entity().timelineCursorA8 = 0;
  }

  void OriginalPlayerController::FUN_00252d88_return_to_idle_state()
  {
    FUN_00225bf0_set_entity_state(0, 1);
    // +0x1B6, the idle fidget's timer, and +0x1B8 forced non-zero.
    entity().idleTimer1b6 = 0;
    if (entity().interactParam1b8 == 0)
    {
      entity().interactParam1b8 = 1;
    }
    entity().motionFlags1bb &= static_cast<std::uint8_t>(~0x12);
    entity().pendingJumpImpulse = false;
  }


  // FUN_00216140's mailbox, from outside the hit test. Nothing in the port
  // lands a blow on the lead yet, so the harness's damage keys go through the
  // same four fields a real contact would write.
  void OriginalPlayerController::FUN_00216140_stamp_hit(std::uint16_t damage,
                                                        std::uint8_t reaction,
                                                        std::uint16_t reactionFrames,
                                                        float fromDirection)
  {
    entity().pendingDamageBe = static_cast<std::uint16_t>(entity().pendingDamageBe + damage);
    entity().hitReactionBc = reaction;
    entity().hitSourceC0 = reactionFrames;
    entity().hitDirectionC4 = fromDirection;
  }

  // FUN_00257C10 / FUN_00257C40 / FUN_00257BE0: three cues off the same table,
  // FUN_00251C80(entity, index), which offsets a sound index by the character's
  // class. 0x13 is the hurt cry, 0x12 the lighter one and 0x15 the landing.
  void OriginalPlayerController::playCharacterCue(int soundIndex)
  {
    if (!FUN_00267d38_playSound_)
    {
      return;
    }
    FUN_00267d38_playSound_(
        orphen::ported::entity::FUN_00251c80_character_cue(entity().typeId00, soundIndex),
        entity());
  }

  // FUN_002536A8. Only reached from a reaction taken in states 3..6 -- the
  // states where the player is attached to something -- and its job is to get
  // it off: drop the held flags, shove it DAT_00352884 along its own facing
  // plus pi, and release the manual camera.
  void OriginalPlayerController::FUN_002536a8_break_out_of_state()
  {
    constexpr float kDAT_00352880_pushAngle = 3.141592025756836f;
    constexpr float kDAT_00352884_pushDistance = 0.200000003f;
    constexpr float kDAT_00352888_settleStep = 9.99999975e-06f;

    const float angle = entity().facingRadians5c + kDAT_00352880_pushAngle;
    entity().halfword04 = static_cast<std::uint16_t>(entity().halfword04 & 0xFFF7u);
    entity().halfword08 = static_cast<std::uint16_t>(entity().halfword08 | 4u);
    entity().collisionFlags0c &= 0xFFFFFFFEu;
    entity().positionX20 += std::cos(angle) * kDAT_00352884_pushDistance;
    entity().positionZ24 += std::sin(angle) * kDAT_00352884_pushDistance;
    // The FUN_00227798 re-sample and the +0x4C / +0x28 reconciliation under it
    // are left to FUN_002262C0, which runs later on this same frame and does
    // exactly that. FUN_00217E18(0) -- the manual camera release -- is the
    // caller's, for the same reason the pool is.
    entity().rotationX154 = 0.0f;
    entity().desiredDeltaX30 = kDAT_00352888_settleStep;
    entity().idleTimer1b6 = 0;
    // 0x00253778 is `sw $zero, 0x1a0($s0)` -- the climb's turn rate, not the
    // +0x1B0 speed, which the knockback that follows reads as it stands.
    entity().climbTurnRate1a0 = 0.0f;
    if (FUN_00217e18_releaseCamera_)
    {
      FUN_00217e18_releaseCamera_();
    }
    // DAT_003555D1, the push-out suspension FUN_00252DE0 raised.
    orphen::ported::entity::DAT_003555d1_suspendPushOut() = false;
  }

  // FUN_00251ED8:97-227. The lead's own damage drain -- the counterpart of the
  // +0xBE block every enemy wrapper opens with, and the only place the field
  // player's hit points move.
  //
  // It runs before the state dispatch and can replace the state outright, so a
  // hit taken mid-swing ends the swing.
  void OriginalPlayerController::FUN_00251ed8_apply_pending_damage(std::uint32_t frameTicks)
  {
    // :86-96. The red flash the previous hit lit, carried on the body and
    // counted down. FUN_00267DA0 copies +0x20..+0x2B into the slot's first
    // three floats, so the light follows the player while it lasts.
    if (bGpffffbd58_hitLightSlot_ >= 0 && DAT_00343888_lights_ != nullptr)
    {
      auto &light =
          DAT_00343888_lights_->slot(static_cast<std::uint32_t>(bGpffffbd58_hitLightSlot_));
      light.x = entity().positionX20;
      light.y = entity().positionZ24;
      light.z = entity().positionY28;
      uGpffffbd5a_hitLightFrames_ -= 1;
      if (uGpffffbd5a_hitLightFrames_ < 1)
      {
        light.radius = 0.0f;
        bGpffffbd58_hitLightSlot_ = -1;
      }
    }

    if (entity().pendingDamageBe != 0)
    {
      // :98-103. cGpffffb6e4 is the "a cutscene is holding the player" latch;
      // outside one the hit un-hides the body. FUN_0022A418 clears the latch and
      // nothing in src/ ever writes it, so this is the only branch.
      entity().halfword08 = static_cast<std::uint16_t>(entity().halfword08 & 0xFFFEu);

      // :104-110, state 9's carried object, is left out: the port has no state 9
      // and no +0x198 holder to detach.

      // :114. +0xC2 bit 0x2000 is the attack record's "costs nothing" bit. The
      // reaction still plays; only the subtraction is skipped.
      bool survived = true;
      if ((entity().hitFlagsC2 & 0x2000u) == 0)
      {
        // FUN_00257B00 is FUN_0023BBD8(0, 0x13), the pad rumble. Not ported --
        // the port has no rumble path at all.
        const std::int32_t before = static_cast<std::int16_t>(entity().staggerTimer12a);
        const std::int32_t after = before - static_cast<std::int32_t>(entity().pendingDamageBe);
        entity().staggerTimer12a = static_cast<std::uint16_t>(after);
        survived = static_cast<std::int16_t>(entity().staggerTimer12a) > 0;
      }

      if (!survived)
      {
        // :122-141, the death. `psVar8` is a `short *`, so `psVar8[2]` is
        // **+0x04**, not +0x02: bit 0 drops the entity out of the collision
        // clamp and bit 4 out of the hit tests. +0x0C bit 0 takes it off the
        // ground, +0x134 goes to *zero* -- FUN_002555D8 ramps it back up two a
        // frame while the body flies -- and +0x62 is the 30 frames it then
        // spends face down before FUN_002555D8 hands it to the game over.
        entity().halfword04 = static_cast<std::uint16_t>(entity().halfword04 | 0x11u);
        entity().collisionFlags0c &= 0xFFFFFFFEu;
        entity().fadeRamp62 = 0x3C0;
        entity().staggerTimer12a = 0;
        entity().fadeLevel134 = 0;
        // FUN_00265EC0(0x58CD70) releases the field HP gauge, and
        // FUN_00205938(7, 0x2F, 0) plus the two FUN_00206260 ramps are the death
        // sting and the music fade. All three are the caller's: the pool and the
        // sequencer are outside a controller bound to one slot.
        if (onDeath_)
        {
          onDeath_();
        }
        // Dying on a hazard surface -- the one state that is already 0x19 --
        // gets no horizontal throw, because the body is meant to drop where it
        // is rather than be launched off the edge that killed it.
        if (entity().state60 == kStateTerrainHazard)
        {
          entity().playerSpeed1b0 = 0.0f;
        }
        else
        {
          entity().playerSpeed1b0 = kuGpffff88c0_deathKnockback;
        }
        entity().verticalVelocity44 = kuGpffff88c8_deathPopUp;
        entity().facingRadians5c = entity().hitDirectionC4 + kfGpffff88c4_pi;
        // **The knockback, not a death state of its own.** FUN_00225BF0's
        // arguments here are literally 0x18 and 0x20, so the last thing the
        // player does is the same tumble any hard hit produces -- and
        // FUN_002555D8 is what notices, thirty frames later, that it never got
        // up, because +0x12A is zero.
        FUN_00225bf0_set_entity_state(kStateHitKnockback, kAnimationHitKnockback);
        playCharacterCue(0x13);
      }
      else
      {
        // :146-207. Which reaction, and what it costs the state the player was
        // already in.
        const std::int16_t state = static_cast<std::int16_t>(entity().state60);
        bool handled = false;
        if (state >= 3 && state < 7)
        {
          // An attached state: break out of it first, and the reaction is the
          // plain knockback whatever +0xBC asked for.
          FUN_002536a8_break_out_of_state();
          FUN_00225bf0_set_entity_state(kStateHitKnockback, kAnimationHitKnockback);
          entity().velocityX3c = 0.0f;
          entity().velocityZ40 = 0.0f;
          entity().fadeRamp62 = entity().hitSourceC0;
          playCharacterCue(0x13);
          handled = true;
        }
        else if (state == kStateSwordAttack && entity().actionEffect198 >= 0 &&
                 actionEffect_.retireSwordBlade)
        {
          // :161-168. A hit during the swing retires the blade. The original
          // tests the effect entity's type for 0x42 first; the callback carries
          // that test, because it is the side that can see the pool.
          actionEffect_.retireSwordBlade(entity().actionEffect198);
          entity().actionEffect198 = -1;
        }

        if (!handled)
        {
          entity().facingRadians5c = entity().hitDirectionC4 + kfGpffff88cc_pi;
          if (entity().hitReactionBc == kHitReactionFlatten)
          {
            // :176-188. Squashed: the body is scaled and shrunk to nothing for
            // +0xC0 frames, and FUN_002555A8 puts both back.
            FUN_00225bf0_set_entity_state(kStateHitFlatten, kAnimationHitFlatten);
            entity().playerSavedScaleZ1a4 = entity().scaleZ150;
            entity().playerSavedHeight1a8 = entity().height58;
            entity().scaleZ150 = 0.0f;
            entity().height58 = 0.0f;
            if (entity().hitSourceC0 == 0)
            {
              entity().hitSourceC0 = 0x1E;
            }
            playCharacterCue(0x12);
          }
          else if (entity().hitReactionBc == kHitReactionKnockback ||
                   (entity().collisionFlags0c & 1u) == 0)
          {
            // :189-203. Knocked back, either because the attack asked for it or
            // because the player was off the ground when it landed.
            FUN_00225bf0_set_entity_state(kStateHitKnockback, kAnimationHitKnockback);
            entity().collisionFlags0c &= 0xFFFFFFFEu;
            entity().fadeRamp62 = entity().hitSourceC0 != 0 ? entity().hitSourceC0 : 0x100;
            entity().playerSpeed1b0 = entity().hitReactionBc == kHitReactionKnockback
                                                   ? kuGpffff88d0_knockbackSpeed
                                                   : 0.0f;
            entity().verticalVelocity44 = kuGpffff88d4_knockbackPopUp;
            playCharacterCue(0x13);
          }
          else
          {
            // :204-207. The ordinary stagger.
            FUN_00225bf0_set_entity_state(kStateHitStagger, kAnimationHitStagger);
            playCharacterCue(0x12);
          }
          entity().interactParam1b8 = 0x1680;
        }
      }

      // :208-219. The red flash, out of the high half of the light table.
      if (DAT_00343888_lights_ != nullptr)
      {
        if (bGpffffbd58_hitLightSlot_ < 0)
        {
          bGpffffbd58_hitLightSlot_ = DAT_00343888_lights_->FUN_00266008_allocateFromThree();
        }
        if (bGpffffbd58_hitLightSlot_ >= 0)
        {
          const auto index = static_cast<std::uint32_t>(bGpffffbd58_hitLightSlot_);
          uGpffffbd5a_hitLightFrames_ = 0xF;
          auto &light = DAT_00343888_lights_->slot(index);
          light.red = 0xFF;
          light.green = 0;
          light.blue = 0;
          light.radius = 1.0f;
          DAT_00343888_lights_->noteRadius(index, 1.0f);
        }
      }

      // :220-224. The mailbox is emptied, and +0xC0 turns from a frame count
      // into a tick count -- 32 ticks per frame, the unit every other countdown
      // in the engine runs in.
      entity().hitFlagsC2 = 0;
      entity().pendingDamageBe = 0;
      entity().hitSourceC0 = static_cast<std::uint16_t>(entity().hitSourceC0 << 5);
    }

    // :226-230. That tick count, spent. FUN_002555A8 is what waits on it.
    if (entity().hitSourceC0 != 0)
    {
      const std::int32_t remaining =
          static_cast<std::int32_t>(entity().hitSourceC0) - static_cast<std::int32_t>(frameTicks);
      entity().hitSourceC0 = static_cast<std::uint16_t>(remaining);
      if (remaining < 0)
      {
        entity().hitSourceC0 = 0;
      }
    }
  }

  // PTR_FUN_0031E0E8[0x16], FUN_002554D8. The stagger: drift backwards until
  // the animation finishes, then stand up.
  void OriginalPlayerController::FUN_002554d8_update_hit_stagger(std::uint32_t frameTicks)
  {
    constexpr float kDAT_00352978_driftDivisor = 200000.0f;
    constexpr float kDAT_0035297c_pi = 3.141592025756836f;

    // +0x134 ramps the hit tint up two a frame to 0x78. Nothing draws it yet --
    // see docs/hit_flash_is_entity_0x138.md -- but it is the entity's own state
    // and the death handler reads it back.
    if (entity().fadeLevel134 != 0 && entity().fadeLevel134 < 0x78)
    {
      entity().fadeLevel134 = static_cast<std::uint8_t>(entity().fadeLevel134 + 2);
    }

    if ((entity().flags06 & kAnimationComplete06) != 0)
    {
      FUN_00252d88_return_to_idle_state();
      entity().fadeLevel134 = 0;
      entity().interactParam1b8 = 0x1E0;
      return;
    }

    const float angle = entity().facingRadians5c + kDAT_0035297c_pi;
    const float step = (static_cast<float>(frameTicks) * 64.0f) / kDAT_00352978_driftDivisor;
    entity().desiredDeltaX30 += step * std::cos(angle);
    entity().desiredDeltaZ34 += step * std::sin(angle);
  }

  // PTR_FUN_0031E0E8[0x17], FUN_002555A8. The flatten: hold until +0xC0 runs
  // out, put the scale and the height back, stand up.
  void OriginalPlayerController::FUN_002555a8_update_hit_flatten()
  {
    if (static_cast<std::int16_t>(entity().hitSourceC0) > 0)
    {
      return;
    }
    entity().scaleZ150 = entity().playerSavedScaleZ1a4;
    entity().height58 = entity().playerSavedHeight1a8;
    entity().interactParam1b8 = 1;
    FUN_00252d88_return_to_idle_state();
  }

  // PTR_FUN_0031E0E8[0x18], FUN_002555D8. The knockback, as a three-animation
  // chain: 0x20 flying, 0x21 down, 0x23 getting up.
  void OriginalPlayerController::FUN_002555d8_update_hit_knockback(std::uint32_t frameTicks)
  {
    constexpr float kDAT_00352980_knockbackDecay = 0.000156249997f;

    if (entity().fadeLevel134 != 0 && entity().fadeLevel134 < 0x78)
    {
      entity().fadeLevel134 = static_cast<std::uint8_t>(entity().fadeLevel134 + 2);
    }

    const std::uint16_t animation = entity().animationA0;
    if (animation == kAnimationKnockdown)
    {
      // Down. +0x62 is how long it stays there.
      const std::int32_t remaining =
          static_cast<std::int32_t>(entity().fadeRamp62) - static_cast<std::int32_t>(frameTicks);
      entity().fadeRamp62 = static_cast<std::uint16_t>(remaining);
      if (static_cast<std::int16_t>(entity().fadeRamp62) < 1)
      {
        if (static_cast<std::int16_t>(entity().staggerTimer12a) != 0)
        {
          orphen::ported::entity::FUN_00225bc8_set_animation(entity(), kAnimationGetUp);
          return;
        }
        // Out of hit points while down: state 0x1A, FUN_00255820, the "dead on
        // the floor" hold. Not ported; the state simply stops being driven.
        entity().state60 = 0x1A;
      }
      return;
    }

    if (animation == kAnimationHitKnockback)
    {
      if ((entity().collisionFlags0c & 1u) != 0)
      {
        // Landed. FUN_00257BE0 is the impact grunt, FUN_00251C80(entity, 0x15).
        orphen::ported::entity::FUN_00225bc8_set_animation(entity(), kAnimationKnockdown);
        playCharacterCue(0x15);
        return;
      }
      if ((entity().collisionFlags0c & 0x262u) == 0)
      {
        if (entity().playerSpeed1b0 != 0.0f)
        {
          const float step = entity().playerSpeed1b0 * static_cast<float>(frameTicks);
          entity().desiredDeltaX30 += step * std::cos(entity().hitDirectionC4);
          entity().desiredDeltaZ34 += step * std::sin(entity().hitDirectionC4);
          entity().playerSpeed1b0 -= kDAT_00352980_knockbackDecay;
          if (entity().playerSpeed1b0 < 0.0f)
          {
            entity().playerSpeed1b0 = 0.0f;
          }
        }
      }
      else
      {
        entity().playerSpeed1b0 = 0.0f;
      }
      return;
    }

    if (animation == kAnimationGetUp && (entity().flags06 & kAnimationComplete06) != 0)
    {
      entity().fadeLevel134 = 0;
      entity().interactParam1b8 = 0x1E0;
      FUN_00252d88_return_to_idle_state();
    }
  }

  // PTR_FUN_0031E0E8[0x19], FUN_002557A0. **Not death.** FUN_00251ED8:60-71 is
  // its only writer and it tests `+0x6C & 0x1000000` -- a terrain word saying
  // the surface kills on contact -- so this is the lava-and-pit state: sink the
  // body while the tint ramps down, hide it, and count to the respawn that puts
  // the player back on the lead trail and charges it a point of damage. The
  // entry is at the top of update(). Checked against the disassembly at
  // 0x002557A0 (a LAB_ block; there is no src/ file): it matches line for line.
  void OriginalPlayerController::FUN_002557a0_update_terrain_hazard(std::uint32_t frameTicks)
  {
    constexpr float kDAT_00352984_sinkPerFrame = 0.100000001f;
    // The original runs this one off the frame, not off DAT_003555BC: neither
    // branch reads the tick count.
    (void)frameTicks;

    if (entity().fadeLevel134 >= 8)
    {
      entity().fadeLevel134 = static_cast<std::uint8_t>(entity().fadeLevel134 - 4);
      entity().positionY28 -= kDAT_00352984_sinkPerFrame;
      entity().groundHeight4c = entity().positionY28;
      return;
    }

    entity().fadeRamp62 = static_cast<std::uint16_t>(entity().fadeRamp62 + 1);
    const std::int16_t counter = static_cast<std::int16_t>(entity().fadeRamp62);
    if (counter == 1)
    {
      entity().fadeLevel134 = 4;
      entity().halfword08 = static_cast<std::uint16_t>(entity().halfword08 | 1u);
      return;
    }
    if (counter >= 0x21 && onDeathRespawn_)
    {
      // FUN_00255E40: walk DAT_00355704 backwards for a primitive carrying
      // neither 0x0800000 nor 0x1000000, drop the player there and charge it
      // the hazard's damage. It needs the lead trail and the map's terrain
      // words, so it is the caller's; with no hook installed the body stays
      // hidden.
      onDeathRespawn_();
    }
  }

  bool OriginalPlayerController::FUN_00256bb8_update_grounded_field_state(std::uint32_t frameTicks,
                                                                          const OriginalPlayerFrameInput &input,
                                                                          const OriginalInteractionProbe &interactionProbe)
  {
    const bool grounded = (entity().collisionFlags0c & kPhysicsFlagGrounded) != 0;
    // sVar1, +0xA0 as the function found it -- the fidget test compares
    // against this, not against whatever the lines above it wrote.
    const std::uint16_t entryAnimation = entity().animationA0;

    // 1. Forced fall. More than fGpffff8a48 above the ground under us hands off
    //    to the airborne state with the falling animation, rather than merely
    //    losing ground contact. This is what makes walking off a ledge work.
    if (entity().positionY28 - entity().previousGroundHeight50 > kOriginalFallHeight)
    {
      entity().motionFlags1bb = static_cast<std::uint8_t>((entity().motionFlags1bb & 0xef) | 2);
      entity().collisionFlags0c &= ~kPhysicsFlagGrounded;
      FUN_00225bf0_set_entity_state(2, kAnimationJumpFall);
      return false;
    }

    // 2. Jump. Not while standing on a party character: +0x68 is what the lead
    //    is riding (FUN_00251ED8's tail carries it along), and a class below 7
    //    there turns the press into nothing.
    const bool jumpPressed = (input.mappedPressedActions & kOriginalMappedActionJump) != 0;
    bool standingOnParty = false;
    if (entityPool_ != nullptr && entity().interactTarget68 >= 0 &&
        entity().interactTarget68 < static_cast<std::int32_t>(orphen::ported::entity::kEntitySlotCount))
    {
      standingOnParty = orphen::ported::entity::FUN_002298d0_character_class(
                            entityPool_->slot(static_cast<std::size_t>(entity().interactTarget68)).typeId00) < 7;
    }
    if (jumpPressed && grounded && !standingOnParty)
    {
      entity().verticalVelocity44 = 0.0f;
      entity().pendingJumpImpulse = true;
      entity().motionFlags1bb = static_cast<std::uint8_t>((entity().motionFlags1bb & 0xef) | 2);
      entity().collisionFlags0c &= ~kPhysicsFlagGrounded;
      // FUN_00256bb8's jump branch plays FUN_00255d88(entity, 2) -- the same
      // surface table, column 2. The takeoff, not the landing.
      if (FUN_00267d38_playSound_)
      {
        FUN_00267d38_playSound_(
            orphen::ported::entity::FUN_00255d88_surface_cue(
                entity().typeId00, currentSurfaceTerrainFlags(), entity().interactTarget68 >= 0,
                orphen::ported::entity::SurfaceSoundKind::Jump),
            entity());
      }
      FUN_00225bf0_set_entity_state(2, kAnimationJumpRise);
      return false;
    }

    // 3. Interact. `uGpffffb68a & 0x40` is Cross, the confirm button, and a hit
    //    returns before locomotion runs -- which is why the character does not
    //    take a step on the frame a chest opens.
    if (input.interactPressed && interactionProbe && interactionProbe())
    {
      return true;
    }

    // 4. Attack, then 5. use -- an `else if`, so both in the same frame is the
    //    attack. Both dispatch on FUN_002298d0 of the lead's *type*: the class
    //    is the character, and each has its own state, or none. A class with
    //    no entry for the button falls through to the idle block below, as if
    //    nothing had been pressed. See original_party_weapons.h for the table.
    if ((input.mappedPressedActions & kOriginalMappedActionAttack) != 0)
    {
      if (FUN_00256bb8_start_attack(frameTicks))
      {
        return false;
      }
    }
    else if ((input.mappedPressedActions & kOriginalMappedActionUse) != 0)
    {
      if (FUN_00256bb8_start_magic())
      {
        return false;
      }
    }

    // 6. Locomotion or idle.
    //
    //   +0x60 = 0;
    //   if (anim != 0x2F || +0x06 & 1) anim = 1;
    //
    // 0x2F is a held idle that is allowed to play out; everything else snaps
    // back to the stand. The fidget is the same test one level down: it is
    // restarted each frame it is still running, and when it completes the
    // timer goes back to zero so the next one is another 0x8000 ticks away.
    entity().state60 = 0;
    if (entity().animationA0 != kAnimationHeldIdle || (entity().flags06 & kAnimationComplete06) != 0)
    {
      entity().animationA0 = kAnimationStand;
    }

    if (!hasMovementInput(input.cameraRelativeMove) || input.stickMagnitude <= 0.0f)
    {
      entity().running = false;

      // The idle fidget fires when the 16-bit tick accumulator rolls past its
      // sign bit: 0x8000 ticks is 1024 frames, about 17 seconds at 60 fps.
      entity().idleTimer1b6 = static_cast<std::uint16_t>(entity().idleTimer1b6 + static_cast<std::uint16_t>(frameTicks));
      if (static_cast<std::int16_t>(entity().idleTimer1b6) < 0)
      {
        if (entity().animationA0 == kAnimationHeldIdle)
        {
          return false;
        }
        // Class 3 has its own fidget.
        const std::uint16_t fidget =
            orphen::ported::entity::FUN_002298d0_character_class(entity().typeId00) == 3
                ? kAnimationClass3Fidget
                : kAnimationIdleFidget;
        if (entryAnimation == fidget)
        {
          if ((entity().flags06 & kAnimationComplete06) == 0)
          {
            entity().animationA0 = fidget;
          }
          else
          {
            entity().idleTimer1b6 = 0;
          }
        }
        else
        {
          entity().animationA0 = fidget;
        }
      }
      return false;
    }

    // Walk below a stick magnitude of 100, run above it.
    entity().running = input.stickMagnitude > kOriginalRunStickThreshold;
    entity().state60 = 1;
    entity().idleTimer1b6 = 0;

    const float speed = entity().running ? kOriginalRunStepPerFrame : kOriginalWalkStepPerFrame;

    // FUN_00256ff8, before the impulse and before this frame's animation is
    // chosen -- so it reads the keyframe the animation step already landed on.
    // The whole footstep mechanism is in there: it fires only on a keyframe
    // carrying 0x100, and the cue comes from the material under the entity.
    orphen::ported::entity::FUN_00256ff8_footstep(entity(), entity().running,
                                                  currentSurfaceTerrainFlags(),
                                                  FUN_00267d38_playSound_);

    // FUN_00256bb8: FUN_00256ab0(iGpffffb64c * speed * 0.03125, entity).
    FUN_00256ab0_apply_movement_impulse(orphen::ported::movementScaleForFrameTicks(frameTicks) * speed,
                                        input.cameraRelativeMove);

    entity().animationA0 = entity().running ? kAnimationRun : kAnimationWalk;
    return false;
  }

  void OriginalPlayerController::FUN_002534d8_update_airborne_state(std::uint32_t frameTicks,
                                                                    const OriginalPlayerFrameInput &input)
  {
    const bool grounded = (entity().collisionFlags0c & kPhysicsFlagGrounded) != 0;

    // FUN_002534D8:12. **The animation is sampled before the moon jump rewrites
    // it**, and every branch below tests the sample rather than the field. So a
    // moon jump out of a fall still takes the 0x0D arm on the frame it fires --
    // it gets that frame's landing checks and its splash cue -- and only reads
    // as a rise from the next frame on.
    const std::uint16_t animation = entity().animationA0;

    // FUN_002534D8:14-17, the moon jump. **This is the original's, not a
    // harness affordance.** Hold the attack button (raw 0x20, Circle) with the
    // debug byte up, and every press of jump (raw 0x80, Square) re-seeds the
    // vertical velocity to the full jump speed and puts the rise animation back:
    //
    //   if (uGpffffbd54 != 0 && (uGpffffb09c & 0x80) != 0) {
    //       +0xA0 = 0xC;  +0x44 = DAT_0035287C;
    //   }
    //
    // Two stores, no state change, no return and no four-frame startup. Tapping
    // jump repeatedly re-seeds the climb, which is what carries the character
    // upward. The port used to zero +0x44 and arm the startup instead, so each
    // tap cancelled the fall and then waited four frames before pushing: it
    // hovered rather than climbed.
    if (uGpffffbd54_moonJumpArmed_ != 0 &&
        (input.uGpffffb09c_pressedThisFrame & kOriginalMappedActionJump) != 0)
    {
      entity().animationA0 = kAnimationJumpRise;
      entity().verticalVelocity44 = kOriginalJumpVelocity;
    }

    if (animation == kAnimationJumpRise)
    {
      // FUN_002534D8:20-23. The first four timeline entries are the crouch, and
      // the state does nothing but steer through them -- the grounded test below
      // is not reached, which is what stops a jump ending on the frame it starts.
      if (entity().timelineCursorA8 < 4)
      {
        FUN_00253488_apply_airborne_control(frameTicks, input);
        return;
      }
      {
        if (entity().timelineCursorA8 == 4 && entity().pendingJumpImpulse)
        {
          entity().verticalVelocity44 = kOriginalJumpVelocity;
          entity().pendingJumpImpulse = false;
          FUN_00253488_apply_airborne_control(frameTicks, input);
          return;
        }

        entity().motionFlags1bb |= 2;
        if (entity().verticalVelocity44 < 0.0f)
        {
          entity().motionFlags1bb = (entity().motionFlags1bb & 0xef) | 2;
          entity().animationA0 = kAnimationJumpFall;
        }
      }

      if (grounded)
      {
        entity().animationA0 = kAnimationLand;
        entity().pendingJumpImpulse = false;
        FUN_00253468_finish_landing();
        return;
      }
    }
    else if (animation == kAnimationJumpFall)
    {
      // FUN_002534D8:56-60. +0x0C bit 0x400 is the splash FUN_002262C0 raises
      // when an actor comes down on a liquid surface. It latches +0x1BB bit 0x10
      // and plays character cue 0x0D, and that latch is what suppresses the
      // ordinary landing thud below. Nothing in the port sets 0x400 yet -- the
      // water branch of FUN_002262C0 is unported -- so this arm is inert, but it
      // is the reason the thud is conditional rather than unconditional.
      if ((entity().collisionFlags0c & 0x0400u) != 0)
      {
        entity().motionFlags1bb |= 0x10;
        playCharacterCue(0x0D);
      }
      if (grounded)
      {
        entity().animationA0 = kAnimationLand;
        entity().pendingJumpImpulse = false;
        FUN_00253468_finish_landing();
        // FUN_002534D8:63-66. The landing thud, FUN_00255D88(entity, 3) -- the
        // same surface table the footsteps and the takeoff read, column 3. Only
        // a fall earns it: a rise that touches down (:41-44) returns without one,
        // and so does a splash, which already played its own.
        if ((entity().motionFlags1bb & 0x10) == 0 && FUN_00267d38_playSound_)
        {
          FUN_00267d38_playSound_(
              orphen::ported::entity::FUN_00255d88_surface_cue(
                  entity().typeId00, currentSurfaceTerrainFlags(),
                  entity().interactTarget68 >= 0,
                  orphen::ported::entity::SurfaceSoundKind::Extra),
              entity());
        }
        return;
      }
    }
    else if (animation == kAnimationLand)
    {
      // FUN_002534D8:50-54. **The landing runs to the end of its animation.**
      // The exit is +0x06 bit 0 -- the timeline reporting complete -- not the
      // ground test: the character is already grounded the moment this animation
      // is chosen, so testing that here ended the state on its first frame and
      // the recovery never played at all.
      if ((entity().flags06 & kAnimationComplete06) != 0)
      {
        FUN_00252d88_return_to_idle_state();
        return;
      }
    }
    else
    {
      entity().pendingJumpImpulse = false;
      FUN_00252d88_return_to_idle_state();
      return;
    }

    FUN_00253488_apply_airborne_control(frameTicks, input);
  }

  // FUN_002560e8. The one gate on the whole of state 0x1C: when the swing
  // animation reports complete, drop back to idle.
  //
  // It also clears the **player's** already-hit set, not the blade's -- the
  // register holding the entity is untouched across the call, which the
  // disassembly at 0x00256108 confirms. That is vestigial for the sword: the
  // set FUN_002148a8 actually reads and writes is the blade's own, and the
  // blade is destroyed at the end of every swing, so it starts each one clear
  // regardless. Reproduced because the write is real and clearing +0x06 bit
  // 0x40 on the player is visible to anything else that latches it.
  //
  // `DAT_00355634 = 0` is the other half, and it is dead: this is the only
  // function in the executable that touches that byte.
  bool OriginalPlayerController::FUN_002560e8_end_on_animation_complete()
  {
    if ((entity().flags06 & kAnimationComplete06) == 0)
    {
      return false;
    }
    orphen::ported::entity::FUN_00215e48_clear_hit_set(entity());
    FUN_00252d88_return_to_idle_state();
    return true;
  }

  // FUN_00256130, PTR_FUN_0031e160[0]: state 0x1C, the grounded sword swing.
  //
  // The whole state is driven off the animation rather than off a timer. The
  // player's own update does nothing but watch three points in the timeline:
  //
  //   the frame the cursor first reaches entry 1   spawn the blade
  //   the frame that entry's duration runs out     play the swing
  //   the entry carrying +0xAA bit 0x200           dissipate the blade
  //   the animation completing                     back to idle
  //
  // Note what is *not* here: no movement, no facing change and no pad reads.
  // The swing is committed the moment it starts, which is why it cannot be
  // steered and why the character slides to a stop rather than turning.
  void OriginalPlayerController::FUN_00256130_update_sword_attack()
  {
    if (FUN_002560e8_end_on_animation_complete())
    {
      return;
    }

    if (entity().timelineCursorA8 == 2)
    {
      // +0xA8 steps by two per timeline entry, so 2 is the second keyframe of
      // animation 0x33 -- the top of the swing.
      if ((entity().flags06 & kAnimationStepped06) != 0)
      {
        const std::int32_t effect = actionEffect_.spawnSwordBlade ? actionEffect_.spawnSwordBlade() : -1;
        if (effect < 0)
        {
          // FUN_00265e28 returning 0. The original abandons the swing rather
          // than playing it bladeless.
          FUN_00252d88_return_to_idle_state();
          return;
        }
        entity().actionEffect198 = effect;
        return;
      }
      if ((entity().flags06 & kAnimationExpired06) != 0)
      {
        if (FUN_00267d38_playSound_)
        {
          FUN_00267d38_playSound_(kSoundCueSwordSwing, entity());
        }
      }
      return;
    }

    // The blade is retired by the animation, not by this state ending: the
    // keyframe carrying bit 0x200 puts it into its dissipate animation, and
    // FUN_002d21b8 frees the slot when that finishes. +0x198 is deliberately
    // left pointing at it -- the original never clears the word, and the type
    // test inside `retire` is what stops a recycled slot being touched.
    if ((entity().flagsAa & kKeyframeEventSwordEnd) != 0 &&
        (entity().flags06 & kAnimationStepped06) != 0 && entity().actionEffect198 >= 0 &&
        actionEffect_.retireSwordBlade)
    {
      actionEffect_.retireSwordBlade(entity().actionEffect198);
    }
  }

  // FUN_002562b0, PTR_FUN_0031e160[1]: state 0x1D, the magic cast.
  //
  // Same shape as the sword swing -- FUN_002560e8 at the top, then two points
  // in animation 0x14's timeline -- but the two points do more, because the
  // projectile exists for the gap between them:
  //
  //   cursor 6   spawn it, charging, at the caster's role-4 bone
  //   cursor 10  launch it: state 1, physics on, cue 0xA6
  //
  // and on every other frame, while it is still charging, its position is
  // rewritten to the hand. It is not attached through +0x192 the way the sword
  // blade is; the caster pushes it, which is why interrupting the cast leaves
  // it where it was rather than dragging it.
  void OriginalPlayerController::FUN_002562b0_update_magic_cast()
  {
    if (FUN_002560e8_end_on_animation_complete())
    {
      return;
    }

    if (entity().timelineCursorA8 == 6 && (entity().flags06 & kAnimationStepped06) != 0)
    {
      const std::int32_t projectile =
          actionEffect_.spawnMagicProjectile ? actionEffect_.spawnMagicProjectile() : -1;
      entity().actionEffect198 = projectile;
      if (projectile < 0)
      {
        // FUN_002d2e00 returning 0: the pool was full, or the floor was above
        // the caster's hand. Either way the cast is dropped.
        FUN_00252d88_return_to_idle_state();
        return;
      }
    }
    else if (entity().timelineCursorA8 == 10 && (entity().flags06 & kAnimationStepped06) != 0)
    {
      if (entity().actionEffect198 < 0)
      {
        FUN_00252d88_return_to_idle_state();
        return;
      }
      const bool launched = actionEffect_.launchMagicProjectile &&
                            actionEffect_.launchMagicProjectile(entity().actionEffect198);
      if (!launched)
      {
        FUN_00252d88_return_to_idle_state();
        return;
      }
      if (FUN_00267d38_playSound_)
      {
        FUN_00267d38_playSound_(kSoundCueMagicLaunch, entity());
      }
      // FUN_00257ad0 -> FUN_0023bbd8(0, 3) also fires here. That is the sound
      // engine's priority-channel entry point rather than FUN_00267d38's, and
      // the port reaches the engine only through the latter, so it is skipped
      // rather than guessed at.
    }

    // Every frame, charging or not: the hold. The callback checks the
    // projectile's own +0x60 -- the original's `+0x60 == 0` gate -- so this is
    // a no-op once it has launched.
    if (entity().actionEffect198 >= 0 && actionEffect_.holdMagicProjectileAtHand)
    {
      actionEffect_.holdMagicProjectileAtHand(entity().actionEffect198);
    }
  }

  orphen::ported::entity::OriginalEntity *OriginalPlayerController::poolSlot(std::int32_t slot)
  {
    if (entityPool_ == nullptr || slot < 0 ||
        slot >= static_cast<std::int32_t>(orphen::ported::entity::kEntitySlotCount))
    {
      return nullptr;
    }
    return &entityPool_->slot(static_cast<std::size_t>(slot));
  }

  // FUN_00256BB8, `uVar2 & 0x20`:
  //
  //   class 0  FUN_00225bf0(0x1C, 0x33)                     the sword
  //   class 3  FUN_00225bf0(0x1E, 0x33), then spawn 0x4E
  //   class 4  FUN_00225bf0(0x20, 199),  then spawn 0x50
  //
  // and nothing for the rest. The state is set *before* the spawn, and a
  // full pool returns without undoing it -- the state then runs with whatever
  // +0x198 already held, as the original does.
  bool OriginalPlayerController::FUN_00256bb8_start_attack(std::uint32_t frameTicks)
  {
    const int characterClass = orphen::ported::entity::FUN_002298d0_character_class(entity().typeId00);
    std::int16_t weaponType = 0;
    if (characterClass == 0)
    {
      FUN_00225bf0_set_entity_state(kStateSwordAttack, kAnimationSwordAttack);
      return true;
    }
    if (characterClass == 3)
    {
      FUN_00225bf0_set_entity_state(kStateClass3Attack, kAnimationSwordAttack);
      weaponType = orphen::ported::entity::kPartyHeldWeapon4eTypeId;
    }
    else if (characterClass == 4)
    {
      FUN_00225bf0_set_entity_state(kStateClass4Attack, 199);
      weaponType = orphen::ported::entity::kPartyHeldWeapon50TypeId;
    }
    else
    {
      return false;
    }
    if (actionEffect_.actorEnvironment)
    {
      const std::int32_t weapon = orphen::ported::entity::FUN_00256bb8_spawn_held_weapon(
          entity(), entityPoolSlot_, weaponType, actionEffect_.actorEnvironment(frameTicks));
      if (weapon >= 0)
      {
        entity().actionEffect198 = weapon;
      }
    }
    return true;
  }

  // FUN_00256BB8, `uVar2 & 0x10`:
  //
  //   class 0  +0x198 = 0; FUN_00225bf0(0x1D, 0x14); FUN_00257b50 (cue 0xA5)
  //   class 3  FUN_00225bf0(0x1F, 0x14); FUN_00257b50
  //   class 4  FUN_00225bf0(0x21, 0x14); +0x19C = +0x198 = 0; +0x06 |= 0x80
  //   class 5  FUN_00225bf0(0x23, 0x14)
  //
  // The +0x198 clear matters for class 0: that word is shared with the sword
  // blade and with the interaction candidate, and FUN_002562b0 tests it to
  // decide whether the cast produced anything.
  bool OriginalPlayerController::FUN_00256bb8_start_magic()
  {
    const int characterClass = orphen::ported::entity::FUN_002298d0_character_class(entity().typeId00);
    const auto castCue = [this]
    {
      if (FUN_00267d38_playSound_)
      {
        FUN_00267d38_playSound_(kSoundCueMagicCast, entity());
      }
    };
    switch (characterClass)
    {
    case 0:
      entity().actionEffect198 = -1;
      FUN_00225bf0_set_entity_state(kStateMagicCast, kAnimationMagicCast);
      castCue();
      return true;
    case 3:
      FUN_00225bf0_set_entity_state(kStateClass3Magic, kAnimationMagicCast);
      castCue();
      return true;
    case 4:
      FUN_00225bf0_set_entity_state(kStateClass4Magic, kAnimationMagicCast);
      entity().actionEffect198 = -1;
      entity().actionEffect19c = -1;
      entity().flags06 = static_cast<std::uint16_t>(entity().flags06 | 0x80u);
      return true;
    case 5:
      FUN_00225bf0_set_entity_state(kStateClass5Magic, kAnimationMagicCast);
      return true;
    default:
      return false;
    }
  }

  // FUN_002563E8, state 0x1E: class 3's three-hit combo.
  //
  //   0x33 --(attack within 10 frames, cursor >= 6)--> 0x30 --(same)--> 0x31
  //
  // with the held weapon stepped through its own animations 4 and 5 in step.
  // The weapon is deleted on the frame 0x31's cursor reaches 10 at a keyframe
  // expiry, and in any case when the lead's animation completes. The swing cue
  // plays on any keyframe expiry carrying +0xAA bit 0x100.
  void OriginalPlayerController::FUN_002563e8_update_class3_attack(std::uint32_t frameTicks,
                                                                   const OriginalPlayerFrameInput &input)
  {
    const std::int32_t weaponSlot = entity().actionEffect198;
    if (FUN_002560e8_end_on_animation_complete())
    {
      // `if (0 < *psVar2) FUN_00265ec0(psVar2)`: whatever is live there.
      auto *weapon = poolSlot(weaponSlot);
      if (weapon != nullptr && weapon->typeId00 > 0 && actionEffect_.actorEnvironment)
      {
        orphen::ported::entity::FUN_00265ec0_destroy_entity(
            static_cast<std::size_t>(weaponSlot), actionEffect_.actorEnvironment(frameTicks));
      }
      return;
    }

    const bool chain = (input.FUN_0023b890_pressedRecent10 & kOriginalMappedActionAttack) != 0;
    const auto cursor = static_cast<std::int16_t>(entity().timelineCursorA8);
    auto *weapon = poolSlot(weaponSlot);
    switch (entity().animationA0)
    {
    case 0x31:
      if (cursor == 10 && (entity().flags06 & kAnimationExpired06) != 0 && weapon != nullptr &&
          actionEffect_.actorEnvironment)
      {
        orphen::ported::entity::FUN_00265ec0_destroy_entity(
            static_cast<std::size_t>(weaponSlot), actionEffect_.actorEnvironment(frameTicks));
      }
      break;
    case 0x30:
      if (cursor >= 6 && chain)
      {
        orphen::ported::entity::FUN_00225bc8_set_animation(entity(), 0x31);
        if (weapon != nullptr)
        {
          orphen::ported::entity::FUN_00225bc8_set_animation(*weapon, 5);
        }
      }
      break;
    case 0x33:
      if (cursor >= 6 && chain)
      {
        orphen::ported::entity::FUN_00225bc8_set_animation(entity(), 0x30);
        if (weapon != nullptr)
        {
          orphen::ported::entity::FUN_00225bc8_set_animation(*weapon, 4);
        }
      }
      break;
    default:
      break;
    }

    if ((entity().flagsAa & kKeyframeEvent100) != 0 && (entity().flags06 & kAnimationExpired06) != 0 &&
        FUN_00267d38_playSound_)
    {
      FUN_00267d38_playSound_(kSoundCueClass3Swing, entity());
    }
  }

  // FUN_00256548, state 0x1F: class 3's orb.
  //
  //   keyframe with +0xAA 0x100, on entry  spawn the orb (FUN_002D06B0) 0.2
  //                                        back on +0x24 and 0.8 up; no orb
  //                                        means back to idle
  //   keyframe with +0xAA 0x200, on entry  launch it: state 1, animation 2,
  //                                        cue 0xA6
  void OriginalPlayerController::FUN_00256548_update_class3_magic(std::uint32_t frameTicks)
  {
    if (FUN_002560e8_end_on_animation_complete())
    {
      return;
    }
    const bool entered = (entity().flags06 & kAnimationStepped06) != 0;
    if ((entity().flagsAa & kKeyframeEvent100) != 0 && entered)
    {
      const orphen::ported::psm2::Vec3 point{entity().positionX20,
                                             entity().positionZ24 - kDAT_0035299c_orbBack,
                                             entity().positionY28 + kDAT_003529a0_orbUp};
      const std::int32_t orb =
          actionEffect_.actorEnvironment
              ? orphen::ported::entity::FUN_002d06b0_spawn_orb(entity(), entityPoolSlot_, point,
                                                               actionEffect_.actorEnvironment(frameTicks))
              : -1;
      entity().actionEffect198 = orb;
      if (orb < 0)
      {
        FUN_00252d88_return_to_idle_state();
      }
      return;
    }
    if ((entity().flagsAa & kKeyframeEventSwordEnd) != 0 && entered && entity().actionEffect198 >= 0)
    {
      if (auto *orb = poolSlot(entity().actionEffect198))
      {
        orphen::ported::entity::FUN_00225bf0_set_state_and_animation(*orb, 1, 2);
      }
      if (FUN_00267d38_playSound_)
      {
        FUN_00267d38_playSound_(kSoundCueMagicLaunch, entity());
      }
    }
  }

  // FUN_00256620, state 0x20: class 4's three-hit combo, 199 -> 200 -> 0xC9.
  //
  // Unlike class 3's, the hit test is run from here, on the held weapon, and
  // only in each swing's active window:
  //
  //   199   cursor 4..6   (`(cursor - 4) < 3`, unsigned -- 0x002566A0 sltiu)
  //   200   cursor 0..2
  //   0xC9  cursor 0..2
  //
  // Outside the window the next press chains, from cursor 12 on 199 and from
  // cursor 6 on 200, clearing the weapon's already-hit set so the next swing
  // can hit the same things again.
  void OriginalPlayerController::FUN_00256620_update_class4_attack(std::uint32_t frameTicks,
                                                                   const OriginalPlayerFrameInput &input)
  {
    const std::int32_t weaponSlot = entity().actionEffect198;
    if (FUN_002560e8_end_on_animation_complete())
    {
      auto *weapon = poolSlot(weaponSlot);
      if (weapon != nullptr && weapon->typeId00 >= 1 && actionEffect_.actorEnvironment)
      {
        orphen::ported::entity::FUN_00265ec0_destroy_entity(
            static_cast<std::size_t>(weaponSlot), actionEffect_.actorEnvironment(frameTicks));
      }
      return;
    }

    auto *weapon = poolSlot(weaponSlot);
    const bool chain = (input.FUN_0023b890_pressedRecent10 & kOriginalMappedActionAttack) != 0;
    const auto cursor = static_cast<std::int16_t>(entity().timelineCursorA8);
    const auto hitTest = [&]
    {
      if (weapon != nullptr && actionEffect_.actorEnvironment)
      {
        // FUN_002148a8(weapon, weapon + 0x198); a contact is FUN_00257ab0,
        // FUN_0023bbd8(0, 3), which the port skips.
        orphen::ported::entity::partyWeaponHitTest(*weapon, static_cast<std::size_t>(weaponSlot),
                                                   weapon->hitParameters198,
                                                   actionEffect_.actorEnvironment(frameTicks));
      }
    };
    const auto chainTo = [&](std::uint16_t next)
    {
      if (weapon != nullptr)
      {
        orphen::ported::entity::FUN_00215e48_clear_hit_set(*weapon);
      }
      orphen::ported::entity::FUN_00225bc8_set_animation(entity(), next);
    };

    switch (entity().animationA0)
    {
    case 200:
      if (cursor > 2)
      {
        if (cursor >= 6 && chain)
        {
          chainTo(0xc9);
        }
      }
      else
      {
        hitTest();
      }
      break;
    case 0xc9:
      if (cursor < 3)
      {
        hitTest();
      }
      break;
    case 199:
      if (static_cast<std::uint32_t>(static_cast<std::int32_t>(entity().timelineCursorA8) - 4) < 3u)
      {
        hitTest();
      }
      else if (cursor >= 12 && chain)
      {
        chainTo(200);
      }
      break;
    default:
      break;
    }

    if ((entity().flagsAa & kKeyframeEventSwordEnd) != 0 && (entity().flags06 & kAnimationExpired06) != 0 &&
        FUN_00267d38_playSound_)
    {
      FUN_00267d38_playSound_(kSoundCueClass4Swing, entity());
    }
  }

  // FUN_002567C0, state 0x21: class 4's throw.
  //
  // Animation 0x14, then 0x11:
  //
  //   0x14 cursor 0, keyframe expiry   the 0x51 prop appears in the hand
  //   0x14 cursor 8, keyframe entry    the 0x52 projectile appears, held
  //   0x14 complete                    lead to 0x11, the prop to its
  //                                    animation 2, and the projectile is let
  //                                    go (+0x94 = 1) with cue 0xEB
  //   0x11 complete                    back to idle, the prop deleted
  //
  // Every frame of 0x14 the projectile is written to the hand, so it rides the
  // animation until the release.
  void OriginalPlayerController::FUN_002567c0_update_class4_magic(std::uint32_t frameTicks)
  {
    if (entity().animationA0 != kAnimationMagicCast)
    {
      if (entity().animationA0 != 0x11)
      {
        return;
      }
      if (!FUN_002560e8_end_on_animation_complete())
      {
        return;
      }
      auto *prop = poolSlot(entity().actionEffect198);
      if (prop != nullptr && prop->typeId00 == orphen::ported::entity::kPartyHandProp51TypeId &&
          actionEffect_.actorEnvironment)
      {
        orphen::ported::entity::FUN_00265ec0_destroy_entity(
            static_cast<std::size_t>(entity().actionEffect198), actionEffect_.actorEnvironment(frameTicks));
      }
      return;
    }
    if (!actionEffect_.actorEnvironment)
    {
      return;
    }
    const auto environment = actionEffect_.actorEnvironment(frameTicks);

    if ((entity().flags06 & kAnimationComplete06) == 0)
    {
      if (entity().timelineCursorA8 == 0 && (entity().flags06 & kAnimationExpired06) != 0)
      {
        const std::int32_t prop =
            orphen::ported::entity::FUN_002567c0_spawn_hand_prop(entity(), entityPoolSlot_, environment);
        if (prop < 0)
        {
          FUN_00252d88_return_to_idle_state();
        }
        else
        {
          entity().actionEffect198 = prop;
          entity().actionEffect19c = -1;
        }
      }
      else if (entity().timelineCursorA8 == 8 && (entity().flags06 & kAnimationStepped06) != 0)
      {
        const std::int32_t shot =
            orphen::ported::entity::FUN_002567c0_spawn_throw(entity(), entityPoolSlot_, environment);
        if (shot >= 0)
        {
          entity().actionEffect19c = shot;
        }
      }
    }
    else
    {
      orphen::ported::entity::FUN_00225bc8_set_animation(entity(), 0x11);
      if (auto *prop = poolSlot(entity().actionEffect198))
      {
        orphen::ported::entity::FUN_00225bc8_set_animation(*prop, 2);
      }
      auto *shot = poolSlot(entity().actionEffect19c);
      if (shot == nullptr)
      {
        return;
      }
      if (FUN_00267d38_playSound_)
      {
        FUN_00267d38_playSound_(kSoundCueClass4Throw, entity());
      }
      shot->spawnParam94 = 1;
    }

    if (auto *shot = poolSlot(entity().actionEffect19c))
    {
      orphen::ported::entity::holdAtHand(*shot, entityPoolSlot_,
                                         orphen::ported::entity::kDAT_0031e0b8_throwHandOffset, environment);
    }
  }

  // FUN_002569D8, state 0x23: class 5's channelled cast. It lasts exactly as
  // long as the magic button is held -- there is no FUN_002560E8 here, so the
  // animation's end does not stop it -- and every keyframe entry carrying
  // +0xAA 0x100 releases one more 0x56 from the hand, with cue 0xA7.
  void OriginalPlayerController::FUN_002569d8_update_class5_magic(std::uint32_t frameTicks,
                                                                  const OriginalPlayerFrameInput &input)
  {
    if ((input.uGpffffb688_heldThisFrame & kOriginalMappedActionUse) == 0)
    {
      FUN_00252d88_return_to_idle_state();
      return;
    }
    if ((entity().flagsAa & kKeyframeEvent100) == 0 || (entity().flags06 & kAnimationStepped06) == 0 ||
        !actionEffect_.actorEnvironment)
    {
      return;
    }
    const std::int32_t shot = orphen::ported::entity::FUN_002569d8_spawn_drift(
        entity(), entityPoolSlot_, actionEffect_.actorEnvironment(frameTicks));
    if (shot >= 0 && FUN_00267d38_playSound_)
    {
      FUN_00267d38_playSound_(kSoundCueClass5Cast, entity());
    }
  }

  void OriginalPlayerController::FUN_00253468_finish_landing()
  {
    entity().pendingJumpImpulse = false;
    if ((entity().motionFlags1bb & 2) != 0)
    {
      entity().motionFlags1bb &= static_cast<std::uint8_t>(~2);
    }
  }

  void OriginalPlayerController::FUN_00253488_apply_airborne_control(std::uint32_t frameTicks,
                                                                     const OriginalPlayerFrameInput &input)
  {
    if (hasMovementInput(input.cameraRelativeMove))
    {
      // FUN_00253488: FUN_00256ab0(DAT_003555bc * DAT_003555e8 * DAT_00352878),
      // where DAT_003555e8 is the analog magnitude.
      const float magnitude = input.stickMagnitude > 0.0f ? input.stickMagnitude : kOriginalFullStickMagnitude;
      FUN_00256ab0_apply_movement_impulse(static_cast<float>(frameTicks) * magnitude * kOriginalAirControlUnit,
                                          input.cameraRelativeMove);
    }
  }

  void OriginalPlayerController::FUN_00256ab0_apply_movement_impulse(float movementStep,
                                                                     const orphen::ported::psm2::Vec3 &cameraRelativeMove)
  {
    const float movementMagnitude = horizontalMagnitude(cameraRelativeMove);
    if (movementMagnitude <= kMovementEpsilon)
    {
      return;
    }

    const float goalFacing = std::atan2(cameraRelativeMove.y, cameraRelativeMove.x);

    // FUN_00256ab0 assigns facing DIRECTLY in the normal case:
    //
    //   else { fVar1 = (fGpffffb0a4 + fGpffffb674) - fGpffff8a44; }
    //   *(param_2 + 0x5c) = FUN_00216690(fVar1);
    //
    // There is no rate limit -- the character turns instantly, which is what
    // makes a stick reversal snap. The FUN_0023a320 lerp in that function is
    // reached only when cGpffffb6e1 == 0x1D, one specific camera sub-mode, and
    // even there the rate is scaled by ABS(cos(stickAngle)) * fGpffff8a40.
    //
    // The caller has already rotated the stick into world space, so the goal
    // stands in for (fGpffffb0a4 + fGpffffb674 - fGpffff8a44).
    if (input0x1dTurnSmoothing_)
    {
      const float maxTurn = kOriginal0x1dTurnRate * std::abs(std::cos(goalFacing));
      const float turnDelta = shortestAngleDelta(entity().facingRadians5c, goalFacing);
      const float step = std::clamp(turnDelta, -maxTurn, maxTurn);
      entity().facingRadians5c = wrapAngle(entity().facingRadians5c + step);
    }
    else
    {
      entity().facingRadians5c = wrapAngle(goalFacing);
    }

    // The impulse follows the facing the entity actually has, so a sharp input
    // change arcs instead of teleporting the velocity.
    const float facingX = std::cos(entity().facingRadians5c);
    const float facingZ = std::sin(entity().facingRadians5c);

    // +0x3C / +0x40: the per-frame velocity the original also publishes.
    entity().velocityX3c = movementStep * facingX;
    entity().velocityZ40 = movementStep * facingZ;
    entity().desiredDeltaX30 += entity().velocityX3c;
    entity().desiredDeltaZ34 += entity().velocityZ40;
  }

  void OriginalPlayerController::FUN_002261e0_step_physics(std::uint32_t frameTicks,
                                                           const OriginalTerrainSampler &terrainSampler)
  {
    // FUN_002261E0:16-19 gates each slot on three things before it calls
    // FUN_002262C0: the descriptor byte being positive, +0x02 bit 0x800 clear,
    // and FUN_00225C90 leaving +0x192 negative. Slot 0 is the lead, so the
    // first is always true and the third is "not parented to anything" -- which
    // it is not while the battle module owns it. The pause bit is the one that
    // matters and it is checked here.
    if ((entity().descriptorFlags02 & 0x0800u) != 0)
    {
      return;
    }
    FUN_002262c0_integrate_physics(frameTicks, terrainSampler);
  }

  OriginalTerrainQuery OriginalPlayerController::terrainQueryForEntity(float bodyBaseHeight) const
  {
    // FUN_00227390 computes the body extent once per call and hands the same
    // pair to all four corner samples, including the ones probing a destination
    // the entity has not reached yet.
    return {entity().rejectTerrainMask74,
            true,
            OriginalTerrainBody{bodyBaseHeight, bodyBaseHeight + entity().height58}};
  }

  std::optional<OriginalTerrainSample> OriginalPlayerController::FUN_00227390_validate_destination(
      float originalX,
      float originalZ,
      float bodyBaseHeight,
      const OriginalTerrainSampler &terrainSampler,
      bool applyRequiredMask) const
  {
    if (!terrainSampler)
    {
      return std::nullopt;
    }

    const OriginalTerrainQuery query = terrainQueryForEntity(bodyBaseHeight);
    const float radius = entity().radius54;
    const std::array<orphen::ported::psm2::Vec3, 4> footprintOffsets{{{-radius, -radius, 0.0f},
                                                                      {radius, -radius, 0.0f},
                                                                      {radius, radius, 0.0f},
                                                                      {-radius, radius, 0.0f}}};
    std::optional<OriginalTerrainSample> highestSample;
    std::uint32_t commonTerrainFlags = 0xffffffff;
    float lowestHeight = 500.0f;

    for (const auto &offset : footprintOffsets)
    {
      const auto sample = terrainSampler(originalX + offset.x,
                                         originalZ + offset.y,
                                         bodyBaseHeight,
                                         query);
      if (!sample.has_value())
      {
        return std::nullopt;
      }

      commonTerrainFlags &= sample->terrainFlags;
      lowestHeight = std::min(lowestHeight, sample->height);
      if (!highestSample.has_value() || highestSample->height < sample->height)
      {
        highestSample = sample;
      }
      else if (highestSample->height == sample->height)
      {
        // FUN_00227390: a corner level with the best so far ORs its word into
        // workspace +0x00 rather than replacing it.
        highestSample->terrainFlags |= sample->terrainFlags;
      }
    }

    if (!highestSample.has_value())
    {
      return std::nullopt;
    }
    highestSample->lowestCornerHeight = lowestHeight;

    if (applyRequiredMask && entity().requiredTerrainMask78 != 0 &&
        (commonTerrainFlags & entity().requiredTerrainMask78) == 0)
    {
      return std::nullopt;
    }

    // The footprint AND is only an input to the require test above. It used to
    // be written back over the sample's own flags, which conflated two separate
    // things: "every corner agrees on this" and "this is the surface we settled
    // on". FUN_002262c0 copies the *settled* record's words into the entity, so
    // overwriting them here left +0x70 reading 0 whenever the footprint spanned
    // two differently flagged triangles -- which is most of the time, and which
    // made every terrain trigger unreachable.
    highestSample->commonFootprintFlags = commonTerrainFlags;
    return highestSample;
  }

  void OriginalPlayerController::FUN_002262c0_integrate_physics(std::uint32_t frameTicks,
                                                                const OriginalTerrainSampler &terrainSampler)
  {
    // FUN_002262c0:0x00226304. The very first thing the original does is copy
    // +0x04 into the workspace and bail on bit 0x100 -- before it clears +0x64,
    // before gravity, before the terrain sample, before the epilogue that spends
    // +0x30/+0x34/+0x38. The entity is simply left exactly where it was put.
    //
    // This is how a cutscene pins an actor to a scripted pose. s01_e012 opens on
    // Orphen lying on a bed at z = -1.224 while the floor under him samples
    // -1.300; eeMemory.bin captured at that moment reads +0x04 = 0x312C and
    // +0x4C = -1.224, i.e. the ground height was never resampled. The same dump
    // taken in the field reads 0x3024, so 0x100 really is toggled for the
    // cutscene rather than being a property of the lead. Without this gate the
    // port re-settled him onto the floor and he sank into the mattress.
    if ((entity().halfword04 & 0x0100u) != 0)
    {
      return;
    }

    // The workspace result word (+0x12C), seeded at :39 and stored over +0x0C
    // at :628. +0x0C itself is read mid-solve as *last* frame's word -- bit 0 at
    // :276, bit 0x10000 at :311 and :582 -- so that copy is kept aside.
    const std::uint32_t previousFlags = entity().collisionFlags0c;
    std::uint32_t result = 0;
    const bool gravityOn = (entity().halfword04 & 0x0008u) == 0;

    // FUN_002262c0:40. Cleared once per solve, whether or not a clamp fires.
    entity().blockedBy64 = 0;

    // FUN_002262c0:97-110, gravity. **It runs on the ground as well.** A
    // standing actor falls a sliver every frame and the landing at :511 puts it
    // back on +0x4C, which is what raises bit 0; a walk down a slope is the
    // same landing against a lower +0x4C. Only +0x04 bit 3 turns it off -- the
    // s14_e002 intro found that gate: FUN_0029C198 raises the bit for the whole
    // carry, and without it the lead accrued a third of a unit of fall per
    // frame under a spline that teleported it back.
    //
    //   dt = (float)DAT_003555bc * 0.125
    //   +0x38 += v*dt - (g*dt)*dt*0.5;  v -= g*dt, nudged off exact zero.
    //
    // The port used to apply this only while airborne and paper over the gap
    // with a 0.05 landing tolerance and a "jump startup" that zeroed +0x38
    // through the crouch. Neither is in the original.
    if (gravityOn)
    {
      const float physicsStep = orphen::ported::physicsStepForFrameTicks(frameTicks);
      const float fall = entity().verticalAcceleration48 * physicsStep;
      entity().desiredDeltaY38 += entity().verticalVelocity44 * physicsStep - fall * physicsStep * 0.5f;
      float velocity = entity().verticalVelocity44 - fall;
      if (velocity == 0.0f)
      {
        velocity = kDAT_00352428_velocityFloor;
      }
      entity().verticalVelocity44 = velocity;
    }
    // Workspace +0x10: the post-gravity speed, never written again, so the dust
    // test below reads the speed the actor hit the ground at rather than the
    // zero the landing leaves behind.
    const float impactVelocity = entity().verticalVelocity44;

    // Not ported for the lead: the +0x0A lift block (:41-91, which needs a
    // dynamic group), the +0xBD freeze (:93) and the embedded-corner push-out
    // (:111-209). The non-player copy in actor_frame_update.cpp has the last two.

    // FUN_002262C0:213-451, the movement loop. Entered only for a non-zero +0x30
    // or +0x34; a body with no horizontal request makes no ground query at all,
    // so +0x4C, +0x6C and +0x70 keep what the last one wrote and the landing
    // below compares against that +0x4C. (The +0x0C bit 0x100 re-query on that
    // path is riding an entity, which the port does not model.) Querying anyway
    // let a climber hanging under a ledge find the ledge and be landed on it.
    //
    // **There is no map-wall query here, and the original does not have one.**
    // FUN_002262c0's only geometry call is FUN_00227390; its four blocker
    // helpers walk DAT_0058beb0, the entity pool, not the map. A move into a
    // wall is refused because the destination's ground scan fails one of the
    // tests below, and nothing else. (The port once ran an invented swept-capsule
    // test that blocked s01_e012's 10 cm door sill and with it a scripted walk.)
    if (entity().desiredDeltaX30 != 0.0f || entity().desiredDeltaZ34 != 0.0f)
    {
      const float startX = entity().positionX20;
      const float startZ = entity().positionZ24;
      // puVar11[0x55] / [0x56]: the request's heading and length as asked --
      // before any clamp -- taken once and never recomputed.
      const float requestHeading = std::atan2(entity().desiredDeltaZ34, entity().desiredDeltaX30);
      const float requestSpeed = std::sqrt(entity().desiredDeltaX30 * entity().desiredDeltaX30 +
                                           entity().desiredDeltaZ34 * entity().desiredDeltaZ34);
      float stepX = entity().desiredDeltaX30;
      float stepZ = entity().desiredDeltaZ34;
      float retrySpeed = requestSpeed;
      int attempt = 0;

      // The entity clamps OR 0x20 / 0x40 into +0x0C as they go. Hand them a
      // word without last frame's pair and carry what they raise into result.
      entity().collisionFlags0c = previousFlags & ~0x60u;

      const auto settleOn = [this](const OriginalTerrainSample &surface, float x, float z) {
        // :266-272. Settling copies the surface's words into the entity: +0x6C
        // is the winning corner's (ties ORed), +0x70 the AND across all four.
        // They are the only way a *surface* reaches the script -- opcode 0x61
        // (FUN_0025f4b8) tests one of them against a mask, +0x70 when the
        // selector's 0x80 bit is set -- which is how a floor panel triggers.
        entity().positionX20 = x;
        entity().positionZ24 = z;
        entity().flagWord6c = surface.terrainFlags;
        entity().flagWord70 = surface.commonFootprintFlags;
        // :266 / :352, `+0x0A = workspace +0x20`. **The followers navigate by
        // this.** When the lead is on another level, FUN_00259520 aims a
        // follower at the centre of the lead's +0x0A primitive and lets the
        // graph find the way there. The lead never wrote it, so it kept
        // whatever the last script placement left -- and in s03_e001 the party
        // pathed to a spot the lead had long since left, arrived, found the
        // lead still out of reach, and re-pathed to the same spot forever,
        // walking on the spot. FUN_00227390 only replaces workspace +0x20 for
        // a corner that names a primitive, hence the guard.
        if (surface.packedPrimitive >= 0)
        {
          entity().groundPrimitive0a = surface.packedPrimitive;
        }
      };

      for (;;)
      {
        // :230-252. The clamps run at the top of every pass, against the step
        // this pass is about to try, so a rotated retry still stops at an actor.
        // Without them the lead walked through chests.
        if (entityPool_ != nullptr)
        {
          orphen::ported::entity::FUN_002262c0_clamp_step_against_entities(*entityPool_, entityPoolSlot_,
                                                                          requestHeading, stepX, stepZ);
        }

        const float feet = entity().positionY28;
        const auto destination = FUN_00227390_validate_destination(startX + stepX, startZ + stepZ, feet,
                                                                   terrainSampler);
        // FUN_00227390's return value, lVar7. It ends
        //   uVar1 = 0; if (w[5] <= entity +0x28) { uVar1 = 1; ...required mask... }
        // so it is **1 only for a surface at or below the feet**. A move onto
        // anything higher never takes the accept path; it falls to the step-up
        // branch below, which is the only way up and which an airborne actor
        // cannot take.
        //
        // The port had this as "accept anything while airborne". A jump beside
        // a ledge then accepted the ledge as the destination, wrote its height
        // into +0x4C and the landing snapped the lead up onto it -- so a jump
        // out of s03_e001's collapsed floor went through the floor around it.
        const bool atOrBelowFeet = destination.has_value() && destination->height <= feet;

        bool accepted = false;
        bool held = false;
        if (!destination.has_value() ||
            entity().groundHeight4c - destination->lowestCornerHeight > entity().maxStepDown7c ||
            entity().maxStepDown7c < destination->height - destination->lowestCornerHeight)
        {
          // :256-260 -> LAB_00226884. +0x7C is 100.0, so in practice this is a
          // footprint corner over nothing: the original reads that corner as
          // 128 and trips the second test, and the port's sampler reports it
          // as no answer at all.
          result |= 0x8002u;
        }
        else if (atOrBelowFeet)
        {
          // :262-306, accepted.
          const float surface = destination->height;
          settleOn(*destination, startX + stepX, startZ + stepZ);
          // :274-292. A grounded actor stepping *down* less than 0.125 is taken
          // down with the step this frame rather than left to fall to it.
          if (gravityOn && entity().verticalVelocity44 < 0.0f && (previousFlags & kPhysicsFlagGrounded) != 0)
          {
            const float drop = surface - entity().groundHeight4c;
            if (drop < 0.0f && -0.125f < drop)
            {
              entity().desiredDeltaY38 += drop;
            }
          }
          entity().groundHeight4c = surface < 128.0f ? surface : entity().groundHeight4c - 0.25f;
          accepted = true;
        }
        else if ((previousFlags & 0x10000u) != 0)
        {
          // :311 false -> LAB_00226988.
          held = true;
        }
        else
        {
          // :311-369, the step up.
          result |= 0x0002u;
          // `+0x28 != +0x50` is an actor that is not settled -- mid-fall,
          // mid-jump -- and it may not step at all. +0x50 is still last
          // frame's +0x4C here; :482 refreshes it after the loop.
          if (feet == entity().previousGroundHeight50 &&
              destination->height - feet < kStepHeightDAT_00352434 &&
              destination->slopeAngle <= entity().slopeLimit80)
          {
            if ((entity().halfword04 & 0x0400u) != 0)
            {
              result &= ~0x0002u;
              held = true;
            }
            else
            {
              // The step is provisional: +0x28 is raised just past the
              // surface and the destination asked again from there, on a step
              // shortened by the slope when +0x04 bit 0x20 is up (the lead's
              // is). Kept only if the re-query lands at or below the raised
              // feet and still under 0.26 above where the actor stood.
              float upX = stepX;
              float upZ = stepZ;
              if ((entity().halfword04 & 0x0020u) != 0)
              {
                const float shorten = std::cos(destination->slopeAngle);
                upX *= shorten;
                upZ *= shorten;
              }
              const float raised = destination->height + kDAT_00352438_stepLift;
              const auto above = FUN_00227390_validate_destination(startX + upX, startZ + upZ, raised,
                                                                   terrainSampler);
              if (above.has_value() && above->height <= raised &&
                  above->height - feet < kStepHeightDAT_00352434)
              {
                settleOn(*above, startX + upX, startZ + upZ);
                entity().desiredDeltaY38 -= above->height - feet;
                result &= ~0x0002u;
                entity().groundHeight4c = above->height;
                entity().positionY28 = above->height;
                accepted = true;
              }
            }
          }
        }

        if (accepted)
        {
          break;
        }
        if (held)
        {
          // LAB_00226988: no move, a small lift, and bit 0x10000.
          entity().desiredDeltaY38 = kDAT_00352430_holdLift;
          result |= 0x10000u;
          break;
        }

        // LAB_00226b00, refused. **A refused move is retried on a rotated
        // heading, not split per axis** -- +0x04 bit 2 admits the actor to the
        // ladder (the lead's 0x3024 has it), and the five rungs are the ones
        // documented on the non-player copy in actor_frame_update.cpp:
        //   heading x0.3, +20 deg x0.7, -20 deg (speed kept), +60 deg x0.5,
        //   -60 deg (speed kept), then give up.
        // The port used to try X alone and then Z alone, and record the result
        // in 0x20 / 0x40, which belong to the entity clamps.
        if ((entity().halfword04 & 0x0004u) == 0 || attempt > 4)
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
          retrySpeed = requestSpeed * 0.5f;
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
        result = (result & 0xFFFF7FFDu) | 0x4000u;
      }

      result |= entity().collisionFlags0c & 0x60u;
    }

    // FUN_002262c0:482. +0x50 takes +0x4C every frame, moved or not.
    entity().previousGroundHeight50 = entity().groundHeight4c;

    // FUN_002262c0:479-521, the vertical settle. Workspace +0x0C is +0x28 as
    // the movement loop left it -- a step up has already raised it.
    const float previousY = entity().positionY28;
    const float verticalDelta = entity().desiredDeltaY38;
    if (verticalDelta > 0.0f)
    {
      // A rise is provisional: +0x28 is raised and FUN_00227390 asked again
      // from there, with the required mask zeroed (:486). Anything but 1 -- no
      // ground, or ground above the raised feet -- gives the rise back whole
      // and zeroes the speed. That is what stops a jump at a ceiling.
      entity().positionY28 = previousY + verticalDelta;
      result |= 0x0008u;
      const auto headroom = FUN_00227390_validate_destination(entity().positionX20, entity().positionZ24,
                                                              entity().positionY28, terrainSampler,
                                                              false);
      if (!headroom.has_value() || headroom->height > entity().positionY28)
      {
        result |= 0x000Cu;
        entity().verticalVelocity44 = 0.0f;
        entity().positionY28 = previousY;
      }
    }
    else
    {
      if (verticalDelta < 0.0f)
      {
        result |= 0x0010u;
      }
      // :510-520, `if (z + dz <= +0x4C)`. No tolerance: the stored ground is
      // the whole test.
      entity().positionY28 = previousY + verticalDelta;
      if (entity().positionY28 <= entity().groundHeight4c)
      {
        entity().positionY28 = entity().groundHeight4c;
        entity().verticalVelocity44 = 0.0f;
        result |= kPhysicsFlagGrounded | 0x0004u;
      }
    }

    // :522-533. With gravity off nothing lands, so standing exactly on +0x4C
    // is what counts as grounded.
    const float y = entity().positionY28;
    if (!gravityOn && y == entity().groundHeight4c)
    {
      result |= kPhysicsFlagGrounded;
    }

    // :534-555, +0x2C: the height eased up a step at 0.04 a frame while
    // grounded, and equal to +0x28 otherwise.
    if (y != entity().previousY2c)
    {
      if ((result & kPhysicsFlagGrounded) != 0 && entity().previousY2c < y &&
          y - kDAT_00352454_easeWindow < entity().previousY2c)
      {
        const float eased = entity().previousY2c + kDAT_00352458_easeStep;
        result |= 0x20000u;
        entity().previousY2c = eased <= y ? eased : y;
      }
      else
      {
        entity().previousY2c = y;
      }
    }

    // FUN_002262C0:576-601, immediately before +0x0C is written -- the dust a
    // fall throws up on touchdown. **This is the plume under the game over**:
    // the death launches the body with uGpffff88c8 of pop-up, so it lands like
    // any jump and the landing spawns the same ring.
    //
    //   FUN_0030BD20(v * 128.0) < -4
    //
    // is the whole speed gate. `FUN_0030BD20` is the soft-float conversion, and
    // it shifts the mantissa down before applying the sign -- so it truncates
    // toward zero, exactly as a C cast does, and the real threshold is
    // **v <= -5/128**, not -4/128. That boundary is load-bearing here: the
    // death's fall reaches -0.045 and throws dust, while a plain knockback's
    // reaches -0.034 -- which is -4.35, truncating to -4 -- and does not.
    // Rounding instead of truncating would put dust under the knockback too.
    //
    // It needs this frame grounded (or 0x10000), last frame not, and no
    // killing surface (+0x6C bit 0x1000000).
    if (static_cast<int>(impactVelocity * 128.0f) < -4 && (result & 0x10001u) != 0 &&
        (previousFlags & 0x10001u) == 0 && (entity().flagWord6c & 0x01000000u) == 0)
    {
      // The top nibble of +0x6C is the surface kind. Only 0 and 3 raise dust --
      // the rest are water, which gets FUN_002D4108's ripple instead -- and the
      // kind doubles as the colour: 3 is lit white, 0 is the pool's own default.
      const std::uint32_t surfaceKind = entity().flagWord6c >> 28;
      if ((surfaceKind == 0 || surfaceKind == 3) && FUN_00219af0_landingDust_)
      {
        FUN_00219af0_landingDust_(entity().positionX20, entity().positionZ24,
                                  y - kDAT_0035246c_dustDrop, entity().radius54,
                                  surfaceKind != 0);
      }
      // +0x0C bit 0x800, the "dust went out this frame" marker.
      result |= 0x0800u;
    }

    entity().collisionFlags0c = result;
    entity().desiredDeltaX30 = 0.0f;
    entity().desiredDeltaZ34 = 0.0f;
    entity().desiredDeltaY38 = 0.0f;
  }

} // namespace orphen::ported::player
