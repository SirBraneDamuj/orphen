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

    bool canStepToHeight(float currentHeight, float destinationHeight, float maxStepHeight, bool wasGrounded)
    {
      return !wasGrounded || destinationHeight - currentHeight <= maxStepHeight;
    }

  } // namespace

  void OriginalPlayerController::resetAtOrigin(const OriginalTerrainSampler &terrainSampler)
  {
    resetAt({}, terrainSampler);
  }

  void OriginalPlayerController::resetAt(const orphen::ported::psm2::Vec3 &spawn,
                                         const OriginalTerrainSampler &terrainSampler)
  {
    entity_ = {};
    entity_.positionX20 = spawn.x;
    entity_.positionZ24 = spawn.y;
    entity_.positionY28 = spawn.z;
    entity_.verticalAcceleration48 = kOriginalGravity;
    entity_.radius54 = 0.35f;
    entity_.height58 = 1.25f;
    entity_.maxStepHeight80 = 0.75f;
    FUN_00252d88_return_to_idle_state();

    if (terrainSampler)
    {
      const auto groundSample = terrainSampler(entity_.positionX20,
                                               entity_.positionZ24,
                                               entity_.positionY28,
                                               terrainQueryForEntity());
      if (groundSample.has_value())
      {
        entity_.groundHeight4c = groundSample->height;
        entity_.previousGroundHeight50 = groundSample->height;
        entity_.positionY28 = groundSample->height;
        entity_.previousY2c = groundSample->height;
        entity_.collisionFlags0c = kPhysicsFlagGrounded;
      }
    }
  }

  void OriginalPlayerController::update(std::uint32_t frameTicks,
                                        const OriginalPlayerFrameInput &input,
                                        const OriginalTerrainSampler &terrainSampler,
                                        const OriginalMovementBlocker &movementBlocker)
  {
    // FUN_002000c0 clamps DAT_003555bc to [0x20, 0x80] before anything reads it.
    const std::uint32_t clampedFrameTicks =
        std::clamp(frameTicks, orphen::ported::kMinFrameTicks, orphen::ported::kMaxFrameTicks);
    entity_.desiredDeltaX30 = 0.0f;
    entity_.desiredDeltaZ34 = 0.0f;
    entity_.desiredDeltaY38 = 0.0f;

    if (entity_.state60 == 2)
    {
      FUN_002534d8_update_airborne_state(clampedFrameTicks, input);
    }
    else
    {
      FUN_00256bb8_update_grounded_field_state(clampedFrameTicks, input);
    }

    FUN_002262c0_integrate_physics(clampedFrameTicks, terrainSampler, movementBlocker);

    if (entity_.substateFrameA8 != std::numeric_limits<std::uint16_t>::max())
    {
      ++entity_.substateFrameA8;
    }
  }

  OriginalPlayerSnapshot OriginalPlayerController::snapshot() const
  {
    const bool grounded = (entity_.collisionFlags0c & kPhysicsFlagGrounded) != 0;
    return {{entity_.positionX20, entity_.positionZ24, entity_.positionY28},
            entity_.facingRadians5c,
            entity_.state60,
            entity_.animationA0,
            entity_.substateFrameA8,
            entity_.collisionFlags0c,
            entity_.verticalVelocity44,
            grounded,
            entity_.running};
  }

  void OriginalPlayerController::FUN_00225bf0_set_entity_state(std::uint16_t state, std::uint16_t substate)
  {
    entity_.state60 = state;
    entity_.stateResetA4 = 999;
    entity_.animationA0 = substate;
    entity_.previousSubstateA2 = 0xffff;
    entity_.flags06 &= 0xff38;
    entity_.substateFrameA8 = 0;
  }

  void OriginalPlayerController::FUN_00252d88_return_to_idle_state()
  {
    FUN_00225bf0_set_entity_state(0, 1);
    entity_.motionFlags1bb &= static_cast<std::uint8_t>(~0x12);
    entity_.pendingJumpImpulse = false;
  }

  void OriginalPlayerController::FUN_00256bb8_update_grounded_field_state(std::uint32_t frameTicks,
                                                                          const OriginalPlayerFrameInput &input)
  {
    const bool grounded = (entity_.collisionFlags0c & kPhysicsFlagGrounded) != 0;

    // 1. Forced fall. More than fGpffff8a48 above the ground under us hands off
    //    to the airborne state with the falling animation, rather than merely
    //    losing ground contact. This is what makes walking off a ledge work.
    if (entity_.positionY28 - entity_.previousGroundHeight50 > kOriginalFallHeight)
    {
      entity_.motionFlags1bb = static_cast<std::uint8_t>((entity_.motionFlags1bb & 0xef) | 2);
      entity_.collisionFlags0c &= ~kPhysicsFlagGrounded;
      FUN_00225bf0_set_entity_state(2, kAnimationJumpFall);
      return;
    }

    // 2. Jump. The original also refuses when an equipped item's weapon class is
    //    below 7; there is no inventory here yet.
    const bool jumpPressed = (input.mappedPressedActions & kOriginalMappedActionJump) != 0;
    if (jumpPressed && grounded)
    {
      entity_.verticalVelocity44 = 0.0f;
      entity_.pendingJumpImpulse = true;
      entity_.motionFlags1bb = static_cast<std::uint8_t>((entity_.motionFlags1bb & 0xef) | 2);
      entity_.collisionFlags0c &= ~kPhysicsFlagGrounded;
      FUN_00225bf0_set_entity_state(2, kAnimationJumpRise);
      return;
    }

    // Attack and interact (mapped 0x20 / 0x10) dispatch on weapon class in the
    // original and are not ported; they fall through to locomotion here.

    // 3. Locomotion or idle.
    entity_.state60 = 0;
    entity_.animationA0 = kAnimationStand;

    if (!hasMovementInput(input.cameraRelativeMove) || input.stickMagnitude <= 0.0f)
    {
      entity_.running = false;

      // The idle fidget fires when the 16-bit tick accumulator rolls past its
      // sign bit: 0x8000 ticks is 1024 frames, about 17 seconds at 60 fps.
      const std::uint16_t previousTimer = entity_.idleTimer1b6;
      entity_.idleTimer1b6 = static_cast<std::uint16_t>(previousTimer + static_cast<std::uint16_t>(frameTicks));
      if (static_cast<std::int16_t>(entity_.idleTimer1b6) < 0)
      {
        entity_.animationA0 = kAnimationIdleFidget;
      }
      return;
    }

    // Walk below a stick magnitude of 100, run above it.
    entity_.running = input.stickMagnitude > kOriginalRunStickThreshold;
    entity_.state60 = 1;
    entity_.idleTimer1b6 = 0;

    const float speed = entity_.running ? kOriginalRunStepPerFrame : kOriginalWalkStepPerFrame;

    // FUN_00256bb8: FUN_00256ab0(iGpffffb64c * speed * 0.03125, entity).
    FUN_00256ab0_apply_movement_impulse(orphen::ported::movementScaleForFrameTicks(frameTicks) * speed,
                                        input.cameraRelativeMove);

    entity_.animationA0 = entity_.running ? kAnimationRun : kAnimationWalk;
  }

  void OriginalPlayerController::FUN_002534d8_update_airborne_state(std::uint32_t frameTicks,
                                                                    const OriginalPlayerFrameInput &input)
  {
    const bool grounded = (entity_.collisionFlags0c & kPhysicsFlagGrounded) != 0;
    if (entity_.animationA0 == kAnimationJumpRise)
    {
      if (entity_.substateFrameA8 >= 4)
      {
        if (entity_.substateFrameA8 == 4 && entity_.pendingJumpImpulse)
        {
          entity_.verticalVelocity44 = kOriginalJumpVelocity;
          entity_.pendingJumpImpulse = false;
          FUN_00253488_apply_airborne_control(frameTicks, input);
          return;
        }

        entity_.motionFlags1bb |= 2;
        if (entity_.verticalVelocity44 < 0.0f)
        {
          entity_.motionFlags1bb = (entity_.motionFlags1bb & 0xef) | 2;
          entity_.animationA0 = kAnimationJumpFall;
        }
      }

      if (grounded)
      {
        entity_.animationA0 = kAnimationLand;
        entity_.pendingJumpImpulse = false;
        FUN_00253468_finish_landing();
        return;
      }
    }
    else if (entity_.animationA0 == kAnimationJumpFall)
    {
      if (grounded)
      {
        entity_.animationA0 = kAnimationLand;
        entity_.pendingJumpImpulse = false;
        FUN_00253468_finish_landing();
        return;
      }
    }
    else if (entity_.animationA0 == kAnimationLand)
    {
      if (grounded)
      {
        FUN_00252d88_return_to_idle_state();
        return;
      }
    }
    else
    {
      entity_.pendingJumpImpulse = false;
      FUN_00252d88_return_to_idle_state();
      return;
    }

    FUN_00253488_apply_airborne_control(frameTicks, input);
  }

  void OriginalPlayerController::FUN_00253468_finish_landing()
  {
    entity_.pendingJumpImpulse = false;
    if ((entity_.motionFlags1bb & 2) != 0)
    {
      entity_.motionFlags1bb &= static_cast<std::uint8_t>(~2);
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
      const float turnDelta = shortestAngleDelta(entity_.facingRadians5c, goalFacing);
      const float step = std::clamp(turnDelta, -maxTurn, maxTurn);
      entity_.facingRadians5c = wrapAngle(entity_.facingRadians5c + step);
    }
    else
    {
      entity_.facingRadians5c = wrapAngle(goalFacing);
    }

    // The impulse follows the facing the entity actually has, so a sharp input
    // change arcs instead of teleporting the velocity.
    const float facingX = std::cos(entity_.facingRadians5c);
    const float facingZ = std::sin(entity_.facingRadians5c);

    // +0x3C / +0x40: the per-frame velocity the original also publishes.
    entity_.velocityX3c = movementStep * facingX;
    entity_.velocityZ40 = movementStep * facingZ;
    entity_.desiredDeltaX30 += entity_.velocityX3c;
    entity_.desiredDeltaZ34 += entity_.velocityZ40;
  }

  OriginalTerrainQuery OriginalPlayerController::terrainQueryForEntity() const
  {
    return {entity_.rejectTerrainMask74, true};
  }

  std::optional<OriginalTerrainSample> OriginalPlayerController::FUN_00227390_validate_destination(
      float originalX,
      float originalZ,
      const OriginalTerrainSampler &terrainSampler) const
  {
    if (!terrainSampler)
    {
      return std::nullopt;
    }

    const OriginalTerrainQuery query = terrainQueryForEntity();
    const float radius = entity_.radius54;
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
                                         entity_.positionY28,
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

    if (entity_.requiredTerrainMask78 != 0 && (commonTerrainFlags & entity_.requiredTerrainMask78) == 0)
    {
      return std::nullopt;
    }

    highestSample->terrainFlags = commonTerrainFlags;
    return highestSample;
  }

  void OriginalPlayerController::FUN_002262c0_integrate_physics(std::uint32_t frameTicks,
                                                                const OriginalTerrainSampler &terrainSampler,
                                                                const OriginalMovementBlocker &movementBlocker)
  {
    std::uint32_t nextCollisionFlags = 0;
    const bool wasGrounded = (entity_.collisionFlags0c & kPhysicsFlagGrounded) != 0;
    const float startX = entity_.positionX20;
    const float startZ = entity_.positionZ24;
    const float attemptedX = startX + entity_.desiredDeltaX30;
    const float attemptedZ = startZ + entity_.desiredDeltaZ34;

    auto validateMove = [&](float fromX, float fromZ, float toX, float toZ) -> std::optional<OriginalTerrainSample>
    {
      if (movementBlocker && movementBlocker(fromX,
                                             fromZ,
                                             toX,
                                             toZ,
                                             entity_.positionY28,
                                             entity_.height58,
                                             entity_.radius54))
      {
        return std::nullopt;
      }

      auto ground = FUN_00227390_validate_destination(toX, toZ, terrainSampler);
      if (!ground.has_value() || !canStepToHeight(entity_.positionY28, ground->height, entity_.maxStepHeight80, wasGrounded))
      {
        return std::nullopt;
      }
      return ground;
    };

    std::optional<OriginalTerrainSample> destinationGround = validateMove(startX, startZ, attemptedX, attemptedZ);
    if (destinationGround.has_value())
    {
      entity_.positionX20 = attemptedX;
      entity_.positionZ24 = attemptedZ;
      entity_.groundHeight4c = destinationGround->height;
    }
    else
    {
      nextCollisionFlags |= kPhysicsFlagBlocked;
      destinationGround.reset();

      if (std::abs(entity_.desiredDeltaX30) > kMovementEpsilon)
      {
        auto xOnlyGround = validateMove(startX, startZ, attemptedX, startZ);
        if (xOnlyGround.has_value())
        {
          entity_.positionX20 = attemptedX;
          entity_.groundHeight4c = xOnlyGround->height;
          destinationGround = xOnlyGround;
        }
        else
        {
          nextCollisionFlags |= kPhysicsFlagXBlocked;
        }
      }

      if (std::abs(entity_.desiredDeltaZ34) > kMovementEpsilon)
      {
        auto zOnlyGround = validateMove(entity_.positionX20, entity_.positionZ24, entity_.positionX20, attemptedZ);
        if (zOnlyGround.has_value())
        {
          entity_.positionZ24 = attemptedZ;
          entity_.groundHeight4c = zOnlyGround->height;
          destinationGround = zOnlyGround;
        }
        else
        {
          nextCollisionFlags |= kPhysicsFlagZBlocked;
        }
      }

      if (!destinationGround.has_value())
      {
        destinationGround = FUN_00227390_validate_destination(entity_.positionX20, entity_.positionZ24, terrainSampler);
        if (destinationGround.has_value())
        {
          entity_.groundHeight4c = destinationGround->height;
        }
      }
    }

    entity_.previousGroundHeight50 = entity_.groundHeight4c;

    const bool airborneState = entity_.state60 == 2;
    const bool jumpStartup = airborneState && entity_.animationA0 == kAnimationJumpRise && entity_.pendingJumpImpulse && entity_.substateFrameA8 < 4;
    if (jumpStartup)
    {
      entity_.desiredDeltaY38 = 0.0f;
    }
    else if (airborneState || !wasGrounded)
    {
      // FUN_002262c0: dt = (float)DAT_003555bc * 0.125, then
      //   +0x38 += v*dt - (g*dt)*dt*0.5;  v -= g*dt.
      const float physicsStep = orphen::ported::physicsStepForFrameTicks(frameTicks);
      entity_.desiredDeltaY38 += entity_.verticalVelocity44 * physicsStep -
                                 entity_.verticalAcceleration48 * physicsStep * physicsStep * 0.5f;
      entity_.verticalVelocity44 -= entity_.verticalAcceleration48 * physicsStep;
    }
    else
    {
      entity_.verticalVelocity44 = 0.0f;
      entity_.desiredDeltaY38 = 0.0f;
    }

    const float previousY = entity_.positionY28;
    float attemptedY = entity_.positionY28 + entity_.desiredDeltaY38;
    if (entity_.verticalVelocity44 > 0.0f)
    {
      nextCollisionFlags |= kPhysicsFlagRising;
    }
    else if (entity_.verticalVelocity44 < 0.0f)
    {
      nextCollisionFlags |= kPhysicsFlagFalling;
    }

    if (!jumpStartup && destinationGround.has_value() && entity_.verticalVelocity44 <= 0.0f &&
        attemptedY <= destinationGround->height + kLandingTolerance)
    {
      attemptedY = destinationGround->height;
      entity_.verticalVelocity44 = 0.0f;
      nextCollisionFlags |= kPhysicsFlagGrounded | kPhysicsFlagVerticalCollision;
    }

    if (!airborneState && (nextCollisionFlags & kPhysicsFlagGrounded) != 0)
    {
      attemptedY = entity_.groundHeight4c;
    }

    entity_.positionY28 = attemptedY;
    entity_.previousY2c = previousY;
    entity_.collisionFlags0c = nextCollisionFlags;
    entity_.desiredDeltaX30 = 0.0f;
    entity_.desiredDeltaZ34 = 0.0f;
    entity_.desiredDeltaY38 = 0.0f;
  }

} // namespace orphen::ported::player
