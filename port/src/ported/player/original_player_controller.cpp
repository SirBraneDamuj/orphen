#include "ported/player/original_player_controller.h"

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
    constexpr float kLandingTolerance = 0.05f;
    constexpr float kMovementEpsilon = 0.0001f;
    constexpr std::uint32_t kPhysicsFlagGrounded = 0x0001;
    constexpr std::uint32_t kPhysicsFlagBlocked = 0x0002;
    constexpr std::uint32_t kPhysicsFlagVerticalCollision = 0x0004;
    constexpr std::uint32_t kPhysicsFlagRising = 0x0008;
    constexpr std::uint32_t kPhysicsFlagFalling = 0x0010;
    constexpr std::uint32_t kPhysicsFlagXBlocked = 0x0020;
    constexpr std::uint32_t kPhysicsFlagZBlocked = 0x0040;

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
    constexpr std::uint16_t kSoundCueSwordSwing = 0xa4;
    // FUN_00257b50 and FUN_00257b40, the two halves of the cast: 0xA5 when the
    // state starts and 0xA6 when the projectile leaves the hand.
    constexpr std::uint16_t kSoundCueMagicCast = 0xa5;
    constexpr std::uint16_t kSoundCueMagicLaunch = 0xa6;

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

    bool canStepToHeight(float currentHeight, float destinationHeight, bool wasGrounded)
    {
      return !wasGrounded || destinationHeight - currentHeight < kStepHeightDAT_00352434;
    }

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
    else
    {
      FUN_00256bb8_update_grounded_field_state(clampedFrameTicks, input, interactionProbe);
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
            entity().running};
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
    entity().motionFlags1bb &= static_cast<std::uint8_t>(~0x12);
    entity().pendingJumpImpulse = false;
  }

  void OriginalPlayerController::FUN_00256bb8_update_grounded_field_state(std::uint32_t frameTicks,
                                                                          const OriginalPlayerFrameInput &input,
                                                                          const OriginalInteractionProbe &interactionProbe)
  {
    const bool grounded = (entity().collisionFlags0c & kPhysicsFlagGrounded) != 0;

    // 1. Forced fall. More than fGpffff8a48 above the ground under us hands off
    //    to the airborne state with the falling animation, rather than merely
    //    losing ground contact. This is what makes walking off a ledge work.
    if (entity().positionY28 - entity().previousGroundHeight50 > kOriginalFallHeight)
    {
      entity().motionFlags1bb = static_cast<std::uint8_t>((entity().motionFlags1bb & 0xef) | 2);
      entity().collisionFlags0c &= ~kPhysicsFlagGrounded;
      FUN_00225bf0_set_entity_state(2, kAnimationJumpFall);
      return;
    }

    // 2. Jump. The original also refuses when an equipped item's weapon class is
    //    below 7; there is no inventory here yet.
    const bool jumpPressed = (input.mappedPressedActions & kOriginalMappedActionJump) != 0;
    if (jumpPressed && grounded)
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
      return;
    }

    // 3. Interact. `uGpffffb68a & 0x40` is Cross, the confirm button, and a hit
    //    returns before locomotion runs -- which is why the character does not
    //    take a step on the frame a chest opens.
    if (input.interactPressed && interactionProbe && interactionProbe())
    {
      return;
    }

    // 4. Attack. FUN_00256bb8 dispatches on `FUN_002298d0(*entity)` -- the
    //    entity's *type id*, not an equipped item -- and type 1, the lead
    //    player, answers weapon class 0. Class 0 is the plain sword swing:
    //    state 0x1C, animation 0x33, and nothing else happens here. The blade
    //    itself is spawned a keyframe later by the state handler.
    //
    //    Classes 1..5 are the other things the lead can be holding (a thrown
    //    item spawns type 0x4E or 0x50 here); none of them are reachable while
    //    the port has no inventory, so only class 0 is reproduced.
    if ((input.mappedPressedActions & kOriginalMappedActionAttack) != 0)
    {
      FUN_00225bf0_set_entity_state(kStateSwordAttack, kAnimationSwordAttack);
      return;
    }

    // 5. Use. Triangle, and an `else if` on the attack above -- both in the
    //    same frame is the attack. Class 0 casts the homing magic projectile:
    //    state 0x1D, animation 0x14, and the cue up front rather than partway
    //    through the animation the way the sword's is.
    //
    //    The +0x198 clear matters. That word is shared with the sword blade and
    //    with the interaction candidate, and FUN_002562b0 tests it against zero
    //    to decide whether the cast produced anything; entering with a stale
    //    slot in it would launch whatever was there.
    else if ((input.mappedPressedActions & kOriginalMappedActionUse) != 0)
    {
      entity().actionEffect198 = -1;
      FUN_00225bf0_set_entity_state(kStateMagicCast, kAnimationMagicCast);
      // FUN_00257b50: FUN_00267d38(0xA5, entity), the cast.
      if (FUN_00267d38_playSound_)
      {
        FUN_00267d38_playSound_(kSoundCueMagicCast, entity());
      }
      return;
    }

    // 6. Locomotion or idle.
    entity().state60 = 0;
    entity().animationA0 = kAnimationStand;

    if (!hasMovementInput(input.cameraRelativeMove) || input.stickMagnitude <= 0.0f)
    {
      entity().running = false;

      // The idle fidget fires when the 16-bit tick accumulator rolls past its
      // sign bit: 0x8000 ticks is 1024 frames, about 17 seconds at 60 fps.
      const std::uint16_t previousTimer = entity().idleTimer1b6;
      entity().idleTimer1b6 = static_cast<std::uint16_t>(previousTimer + static_cast<std::uint16_t>(frameTicks));
      if (static_cast<std::int16_t>(entity().idleTimer1b6) < 0)
      {
        entity().animationA0 = kAnimationIdleFidget;
      }
      return;
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
  }

  void OriginalPlayerController::FUN_002534d8_update_airborne_state(std::uint32_t frameTicks,
                                                                    const OriginalPlayerFrameInput &input)
  {
    const bool grounded = (entity().collisionFlags0c & kPhysicsFlagGrounded) != 0;

    // Harness debug affordance, not something the original's airborne state
    // does: holding Circle re-arms the jump button in mid-air. It restarts
    // state 2 / animation 0x0C exactly as FUN_00256bb8's grounded branch does,
    // so the jump runs the same 4-frame startup and the same +0x44 seed --
    // which is what makes it useful for reaching a ceiling to test against.
    if (input.debugMidairJumpHeld && (input.mappedPressedActions & kOriginalMappedActionJump) != 0)
    {
      entity().verticalVelocity44 = 0.0f;
      entity().pendingJumpImpulse = true;
      entity().motionFlags1bb = static_cast<std::uint8_t>((entity().motionFlags1bb & 0xef) | 2);
      entity().collisionFlags0c &= ~kPhysicsFlagGrounded;
      FUN_00225bf0_set_entity_state(2, kAnimationJumpRise);
      FUN_00253488_apply_airborne_control(frameTicks, input);
      return;
    }

    if (entity().animationA0 == kAnimationJumpRise)
    {
      if (entity().timelineCursorA8 >= 4)
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
    else if (entity().animationA0 == kAnimationJumpFall)
    {
      if (grounded)
      {
        entity().animationA0 = kAnimationLand;
        entity().pendingJumpImpulse = false;
        FUN_00253468_finish_landing();
        return;
      }
    }
    else if (entity().animationA0 == kAnimationLand)
    {
      if (grounded)
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
  // animation reports complete, drop back to idle. The original also does
  // `DAT_00355634 = 0` and calls FUN_00215e48, which clear the swing's
  // already-hit set (eight words at entity +0xCC, plus +0x06 bit 0x40) so the
  // next swing can hit the same target again. There is no hit test here to
  // feed, so neither is reproduced.
  bool OriginalPlayerController::FUN_002560e8_end_on_animation_complete()
  {
    if ((entity().flags06 & kAnimationComplete06) == 0)
    {
      return false;
    }
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
      const OriginalTerrainSampler &terrainSampler) const
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
      if (!highestSample.has_value() || highestSample->height < sample->height)
      {
        highestSample = sample;
      }
    }

    if (!highestSample.has_value())
    {
      return std::nullopt;
    }

    if (entity().requiredTerrainMask78 != 0 && (commonTerrainFlags & entity().requiredTerrainMask78) == 0)
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

    std::uint32_t nextCollisionFlags = 0;
    const bool wasGrounded = (entity().collisionFlags0c & kPhysicsFlagGrounded) != 0;
    const float startX = entity().positionX20;
    const float startZ = entity().positionZ24;
    const float attemptedX = startX + entity().desiredDeltaX30;
    const float attemptedZ = startZ + entity().desiredDeltaZ34;

    // **There is no map-wall query here, and the original does not have one.**
    // FUN_002262c0's only geometry call is FUN_00227390; its four blocker
    // helpers (FUN_00228380 / FUN_002285d8 / FUN_00228838 / FUN_00228a90) walk
    // DAT_0058beb0, the entity pool, not the map. A move into a wall is refused
    // because the destination's ground scan fails one of the tests below, and
    // nothing else.
    //
    // The port used to run an invented swept-capsule test over every steep
    // triangle (`queryPsm2ActiveBlockerAlong`), which had no FUN_* behind it and
    // rejected anything taller than 5 cm. s01_e012 has a 10 cm door sill at
    // y = 1.9 -- primitive 3497, a 1.0 x 0.1 strip across the doorway -- and
    // that blocked the scripted walk in stream 0xd5e0 permanently, because a
    // cutscene's 0xF0 cannot give up and route around.
    auto validateMove = [&](float fromX, float fromZ, float toX, float toZ) -> std::optional<OriginalTerrainSample>
    {
      (void)fromX;
      (void)fromZ;

      auto ground = FUN_00227390_validate_destination(toX, toZ, entity().positionY28, terrainSampler);
      if (!ground.has_value())
      {
        return std::nullopt;
      }

      // FUN_002262c0:0x00226cb4, the gate in front of the whole upward-step
      // branch: `if ((float)puVar11[2] <= *(float *)(iVar12 + 0x80))`. This is
      // what stops an actor walking up the hull -- the ship's plating samples
      // about 60 degrees and the limit is 50.
      if (ground->slopeAngle > entity().slopeLimit80)
      {
        return std::nullopt;
      }

      if (!canStepToHeight(entity().positionY28, ground->height, wasGrounded))
      {
        return std::nullopt;
      }
      return ground;
    };

    std::optional<OriginalTerrainSample> destinationGround = validateMove(startX, startZ, attemptedX, attemptedZ);
    if (destinationGround.has_value())
    {
      entity().positionX20 = attemptedX;
      entity().positionZ24 = attemptedZ;
      entity().groundHeight4c = destinationGround->height;
    }
    else
    {
      nextCollisionFlags |= kPhysicsFlagBlocked;
      destinationGround.reset();

      if (std::abs(entity().desiredDeltaX30) > kMovementEpsilon)
      {
        auto xOnlyGround = validateMove(startX, startZ, attemptedX, startZ);
        if (xOnlyGround.has_value())
        {
          entity().positionX20 = attemptedX;
          entity().groundHeight4c = xOnlyGround->height;
          destinationGround = xOnlyGround;
        }
        else
        {
          nextCollisionFlags |= kPhysicsFlagXBlocked;
        }
      }

      if (std::abs(entity().desiredDeltaZ34) > kMovementEpsilon)
      {
        auto zOnlyGround = validateMove(entity().positionX20, entity().positionZ24, entity().positionX20, attemptedZ);
        if (zOnlyGround.has_value())
        {
          entity().positionZ24 = attemptedZ;
          entity().groundHeight4c = zOnlyGround->height;
          destinationGround = zOnlyGround;
        }
        else
        {
          nextCollisionFlags |= kPhysicsFlagZBlocked;
        }
      }

      if (!destinationGround.has_value())
      {
        destinationGround = FUN_00227390_validate_destination(entity().positionX20,
                                                              entity().positionZ24,
                                                              entity().positionY28,
                                                              terrainSampler);
        if (destinationGround.has_value())
        {
          entity().groundHeight4c = destinationGround->height;
        }
      }
    }

    // FUN_002262c0 at 0x00226884 and 0x0022692c: settling on a surface copies
    // that surface's first two words into the entity, at +0x6C and +0x70. They
    // are the only way a *surface* reaches the script -- opcode 0x61
    // (FUN_0025f4b8) tests one of them against a mask, picking +0x70 when the
    // selector's 0x80 bit is set and +0x6C otherwise. That is how a floor panel
    // triggers: there is no trigger entity and no volume, just terrain the
    // player is standing on carrying a flag the scene's per-frame entry watches
    // for. s01_e024's tick makes exactly two such tests.
    //
    // The port had never written either word, so both tests were permanently
    // false and every panel in every scene was dead.
    if (destinationGround.has_value())
    {
      // Both fields take the *whole* 32-bit terrain word, pinned by the EE dump:
      // the enemies hovering over the 0x30010000 floor read 0x30010000 in both
      // +0x6C and +0x70, and the player standing on a 0 floor reads 0 in both.
      //
      // Two earlier guesses were wrong and the dump killed each. +0x6C is not
      // the record's leading word (the player reads 0 where that word is 0xa00),
      // and the pair is not the high and low halves of the terrain word (type
      // 0x62's required mask is 0x00010000, which only overlaps 0x30010000 when
      // the whole word is kept).
      //
      // FUN_002262c0 fills them from two different workspace slots, so they can
      // presumably differ -- probably the surface under each of two sample
      // points. On uniform floor they agree, and the port has nothing that would
      // tell the two apart yet.
      entity().flagWord6c = destinationGround->terrainFlags;
      entity().flagWord70 = destinationGround->terrainFlags;
    }

    entity().previousGroundHeight50 = entity().groundHeight4c;

    const bool airborneState = entity().state60 == 2;
    const bool jumpStartup = airborneState && entity().animationA0 == kAnimationJumpRise && entity().pendingJumpImpulse && entity().timelineCursorA8 < 4;
    if (jumpStartup)
    {
      entity().desiredDeltaY38 = 0.0f;
    }
    else if (airborneState || !wasGrounded)
    {
      // FUN_002262c0: dt = (float)DAT_003555bc * 0.125, then
      //   +0x38 += v*dt - (g*dt)*dt*0.5;  v -= g*dt.
      const float physicsStep = orphen::ported::physicsStepForFrameTicks(frameTicks);
      entity().desiredDeltaY38 += entity().verticalVelocity44 * physicsStep -
                                 entity().verticalAcceleration48 * physicsStep * physicsStep * 0.5f;
      entity().verticalVelocity44 -= entity().verticalAcceleration48 * physicsStep;
    }
    else
    {
      entity().verticalVelocity44 = 0.0f;
      entity().desiredDeltaY38 = 0.0f;
    }

    const float previousY = entity().positionY28;
    float attemptedY = entity().positionY28 + entity().desiredDeltaY38;
    if (entity().verticalVelocity44 > 0.0f)
    {
      nextCollisionFlags |= kPhysicsFlagRising;
    }
    else if (entity().verticalVelocity44 < 0.0f)
    {
      nextCollisionFlags |= kPhysicsFlagFalling;
    }

    // FUN_002262c0 at 0x00226cb4: an upward step is provisional. The original
    // writes the raised height into entity +0x28 in the delay slot of the
    // FUN_00227390 call, so the query is posed from where the actor is trying
    // to get to. If it comes back 0 -- no ground, or ground above the feet --
    // the rise is given back whole (+0x28 restored from workspace +0x0C) and
    // the vertical velocity is zeroed, flagging 0x4 alongside the 0x8 rise.
    // That is the only place upward motion is cancelled, and it is what stops
    // a jump at a room's ceiling instead of passing through it.
    if (entity().desiredDeltaY38 > 0.0f)
    {
      const auto headroom = FUN_00227390_validate_destination(entity().positionX20,
                                                              entity().positionZ24,
                                                              attemptedY,
                                                              terrainSampler);
      if (!headroom.has_value() || headroom->height > attemptedY)
      {
        nextCollisionFlags |= kPhysicsFlagVerticalCollision;
        entity().verticalVelocity44 = 0.0f;
        attemptedY = previousY;
      }
    }

    if (!jumpStartup && destinationGround.has_value() && entity().verticalVelocity44 <= 0.0f &&
        attemptedY <= destinationGround->height + kLandingTolerance)
    {
      attemptedY = destinationGround->height;
      entity().verticalVelocity44 = 0.0f;
      nextCollisionFlags |= kPhysicsFlagGrounded | kPhysicsFlagVerticalCollision;
    }

    if (!airborneState && (nextCollisionFlags & kPhysicsFlagGrounded) != 0)
    {
      attemptedY = entity().groundHeight4c;
    }

    entity().positionY28 = attemptedY;
    entity().previousY2c = previousY;
    entity().collisionFlags0c = nextCollisionFlags;
    entity().desiredDeltaX30 = 0.0f;
    entity().desiredDeltaZ34 = 0.0f;
    entity().desiredDeltaY38 = 0.0f;
  }

} // namespace orphen::ported::player
