#include "ported/player/original_player_controller.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace orphen::ported::player
{
  namespace
  {

    constexpr float kRunSpeed = 6.875f;
    constexpr float kAirControlSpeed = 3.25f;
    constexpr float kJumpVelocity = 8.0f;
    constexpr float kGravity = 24.0f;
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

    bool canStepToHeight(float currentHeight, float destinationHeight, float maxStepHeight, bool wasGrounded)
    {
      return !wasGrounded || destinationHeight - currentHeight <= maxStepHeight;
    }

  } // namespace

  void OriginalPlayerController::resetAtOrigin(const OriginalTerrainSampler &terrainSampler)
  {
    entity_ = {};
    entity_.verticalAcceleration48 = kGravity;
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

  void OriginalPlayerController::update(float deltaSeconds,
                                        const OriginalPlayerFrameInput &input,
                                        const OriginalTerrainSampler &terrainSampler)
  {
    const float clampedDeltaSeconds = std::clamp(deltaSeconds, 0.0f, 0.1f);
    entity_.desiredDeltaX30 = 0.0f;
    entity_.desiredDeltaZ34 = 0.0f;
    entity_.desiredDeltaY38 = 0.0f;

    if (entity_.state60 == 2)
    {
      FUN_002534d8_update_airborne_state(clampedDeltaSeconds, input);
    }
    else
    {
      FUN_00256bb8_update_grounded_field_state(clampedDeltaSeconds, input);
    }

    FUN_002262c0_integrate_physics(clampedDeltaSeconds, terrainSampler);

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
            entity_.substateA0,
            entity_.collisionFlags0c,
            grounded};
  }

  void OriginalPlayerController::FUN_00225bf0_set_entity_state(std::uint16_t state, std::uint16_t substate)
  {
    entity_.state60 = state;
    entity_.stateResetA4 = 999;
    entity_.substateA0 = substate;
    entity_.previousSubstateA2 = 0xffff;
    entity_.flags06 &= 0xff38;
    entity_.substateFrameA8 = 0;
  }

  void OriginalPlayerController::FUN_00252d88_return_to_idle_state()
  {
    FUN_00225bf0_set_entity_state(0, 1);
    entity_.motionFlags1bb &= static_cast<std::uint8_t>(~0x12);
  }

  void OriginalPlayerController::FUN_00256bb8_update_grounded_field_state(float deltaSeconds,
                                                                          const OriginalPlayerFrameInput &input)
  {
    const bool grounded = (entity_.collisionFlags0c & kPhysicsFlagGrounded) != 0;
    const bool jumpPressed = (input.mappedPressedActions & kOriginalMappedActionJump) != 0;
    if (jumpPressed && grounded)
    {
      entity_.verticalVelocity44 = kJumpVelocity;
      entity_.motionFlags1bb = (entity_.motionFlags1bb & 0xef) | 2;
      entity_.collisionFlags0c &= ~kPhysicsFlagGrounded;
      FUN_00225bf0_set_entity_state(2, 0x0c);
      return;
    }

    if (hasMovementInput(input.cameraRelativeMove))
    {
      FUN_00256ab0_apply_movement_impulse(kRunSpeed * deltaSeconds, input.cameraRelativeMove);
      if (entity_.state60 == 0)
      {
        FUN_00225bf0_set_entity_state(1, 1);
      }
      return;
    }

    if (grounded && entity_.state60 != 0)
    {
      FUN_00252d88_return_to_idle_state();
    }
  }

  void OriginalPlayerController::FUN_002534d8_update_airborne_state(float deltaSeconds,
                                                                    const OriginalPlayerFrameInput &input)
  {
    const bool grounded = (entity_.collisionFlags0c & kPhysicsFlagGrounded) != 0;
    if (entity_.substateA0 == 0x0c)
    {
      if (entity_.substateFrameA8 >= 4)
      {
        entity_.motionFlags1bb |= 2;
        if (entity_.verticalVelocity44 < 0.0f)
        {
          entity_.motionFlags1bb = (entity_.motionFlags1bb & 0xef) | 2;
          entity_.substateA0 = 0x0d;
        }
      }

      if (grounded)
      {
        entity_.substateA0 = 0x10;
        FUN_00253468_finish_landing();
        return;
      }
    }
    else if (entity_.substateA0 == 0x0d)
    {
      if (grounded)
      {
        entity_.substateA0 = 0x10;
        FUN_00253468_finish_landing();
        return;
      }
    }
    else if (entity_.substateA0 == 0x10)
    {
      if (grounded)
      {
        FUN_00252d88_return_to_idle_state();
        return;
      }
    }
    else
    {
      FUN_00252d88_return_to_idle_state();
      return;
    }

    FUN_00253488_apply_airborne_control(deltaSeconds, input);
  }

  void OriginalPlayerController::FUN_00253468_finish_landing()
  {
    if ((entity_.motionFlags1bb & 2) != 0)
    {
      entity_.motionFlags1bb &= static_cast<std::uint8_t>(~2);
    }
  }

  void OriginalPlayerController::FUN_00253488_apply_airborne_control(float deltaSeconds,
                                                                     const OriginalPlayerFrameInput &input)
  {
    if (hasMovementInput(input.cameraRelativeMove))
    {
      FUN_00256ab0_apply_movement_impulse(kAirControlSpeed * deltaSeconds, input.cameraRelativeMove);
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

    const float normalizedX = cameraRelativeMove.x / movementMagnitude;
    const float normalizedZ = cameraRelativeMove.y / movementMagnitude;
    entity_.facingRadians5c = std::atan2(normalizedZ, normalizedX);
    entity_.desiredDeltaX30 += movementStep * normalizedX;
    entity_.desiredDeltaZ34 += movementStep * normalizedZ;
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

  void OriginalPlayerController::FUN_002262c0_integrate_physics(float deltaSeconds,
                                                                const OriginalTerrainSampler &terrainSampler)
  {
    std::uint32_t nextCollisionFlags = 0;
    const bool wasGrounded = (entity_.collisionFlags0c & kPhysicsFlagGrounded) != 0;
    const float startX = entity_.positionX20;
    const float startZ = entity_.positionZ24;
    const float attemptedX = startX + entity_.desiredDeltaX30;
    const float attemptedZ = startZ + entity_.desiredDeltaZ34;

    std::optional<OriginalTerrainSample> destinationGround = FUN_00227390_validate_destination(attemptedX, attemptedZ, terrainSampler);
    if (destinationGround.has_value() && canStepToHeight(entity_.positionY28,
                                                         destinationGround->height,
                                                         entity_.maxStepHeight80,
                                                         wasGrounded))
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
        auto xOnlyGround = FUN_00227390_validate_destination(attemptedX, startZ, terrainSampler);
        if (xOnlyGround.has_value() && canStepToHeight(entity_.positionY28, xOnlyGround->height, entity_.maxStepHeight80, wasGrounded))
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
        auto zOnlyGround = FUN_00227390_validate_destination(entity_.positionX20, attemptedZ, terrainSampler);
        if (zOnlyGround.has_value() && canStepToHeight(entity_.positionY28, zOnlyGround->height, entity_.maxStepHeight80, wasGrounded))
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
    if (airborneState || !wasGrounded)
    {
      entity_.desiredDeltaY38 += entity_.verticalVelocity44 * deltaSeconds -
                                 entity_.verticalAcceleration48 * deltaSeconds * deltaSeconds * 0.5f;
      entity_.verticalVelocity44 -= entity_.verticalAcceleration48 * deltaSeconds;
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

    if (destinationGround.has_value() && entity_.verticalVelocity44 <= 0.0f &&
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