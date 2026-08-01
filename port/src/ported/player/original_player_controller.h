#pragma once

#include "ported/psm2/psm2_runtime.h"

#include <cstdint>
#include <functional>
#include <optional>

namespace orphen::ported::player
{

  constexpr std::uint32_t kOriginalMappedActionJump = 0x80;
  constexpr std::uint32_t kOriginalMappedActionAttack = 0x20;
  constexpr std::uint32_t kOriginalMappedActionInteract = 0x10;

  // FUN_00256bb8 animation ids written by the grounded path, and the airborne
  // ids from FUN_002534d8. Entity +0xA0 is an animation id, not a substate.
  constexpr std::uint16_t kAnimationStand = 0x01;
  constexpr std::uint16_t kAnimationWalk = 0x0b;
  constexpr std::uint16_t kAnimationRun = 0x0e;
  constexpr std::uint16_t kAnimationJumpRise = 0x0c;
  constexpr std::uint16_t kAnimationJumpFall = 0x0d;
  constexpr std::uint16_t kAnimationLand = 0x10;
  constexpr std::uint16_t kAnimationIdleFidget = 0x17;

  struct OriginalTerrainSample
  {
    float height = 0.0f;
    std::uint32_t leadingWord = 0;
    std::uint32_t terrainFlags = 0;
    bool sampledByOriginalTerrain = false;
  };

  struct OriginalTerrainQuery
  {
    std::uint32_t rejectTerrainMask = 0;
    bool requireOriginalTerrainSample = true;
  };

  using OriginalTerrainSampler = std::function<std::optional<OriginalTerrainSample>(float originalX,
                                                                                    float originalZ,
                                                                                    float referenceY,
                                                                                    const OriginalTerrainQuery &query)>;

  using OriginalMovementBlocker = std::function<bool(float originalStartX,
                                                     float originalStartZ,
                                                     float originalEndX,
                                                     float originalEndZ,
                                                     float baseY,
                                                     float height,
                                                     float radius)>;

  struct OriginalPlayerFrameInput
  {
    orphen::ported::psm2::Vec3 cameraRelativeMove{};
    std::uint32_t mappedHeldActions = 0;
    std::uint32_t mappedPressedActions = 0;

    // fGpffffb678. FUN_00256bb8 walks at or below 100.0 and runs above it;
    // FUN_00253488 scales air control by it directly. Full deflection is 128.
    float stickMagnitude = 0.0f;
  };

  struct OriginalPlayerSnapshot
  {
    orphen::ported::psm2::Vec3 position{};
    float facingRadians = 0.0f;
    std::uint16_t state = 0;
    std::uint16_t animationId = 0;
    std::uint16_t substateFrame = 0;
    std::uint32_t collisionFlags = 0;
    float verticalVelocity = 0.0f;
    bool grounded = false;
    bool running = false;
  };

  class OriginalPlayerController
  {
  public:
    void resetAtOrigin(const OriginalTerrainSampler &terrainSampler);
    void resetAt(const orphen::ported::psm2::Vec3 &spawn, const OriginalTerrainSampler &terrainSampler);

    // frameTicks is DAT_003555bc: elapsed time in units of 0x20 per 60 Hz frame.
    // See ported/original_frame_timing.h.
    void update(std::uint32_t frameTicks,
                const OriginalPlayerFrameInput &input,
                const OriginalTerrainSampler &terrainSampler,
                const OriginalMovementBlocker &movementBlocker = {});

    OriginalPlayerSnapshot snapshot() const;

  private:
    struct OriginalLeadEntity
    {
      std::uint16_t flags06 = 0;               // +0x06: animation/contact flags used by FUN_002534d8.
      std::uint32_t collisionFlags0c = 0;      // +0x0C: physics result flags from FUN_002262c0; bit 0 is grounded.
      float positionX20 = 0.0f;                // +0x20: world X.
      float positionZ24 = 0.0f;                // +0x24: world Z, mapped to PSM2 horizontal Y in the port.
      float positionY28 = 0.0f;                // +0x28: vertical position.
      float previousY2c = 0.0f;                // +0x2C: previous/smoothed vertical position.
      float desiredDeltaX30 = 0.0f;            // +0x30: per-frame X movement request consumed by physics.
      float desiredDeltaZ34 = 0.0f;            // +0x34: per-frame Z movement request consumed by physics.
      float desiredDeltaY38 = 0.0f;            // +0x38: per-frame vertical delta accumulated by physics.
      float velocityX3c = 0.0f;                // +0x3C: per-frame X velocity published by FUN_00256ab0.
      float velocityZ40 = 0.0f;                // +0x40: per-frame Z velocity published by FUN_00256ab0.
      float facingRadians5c = 0.0f;            // +0x5C: facing angle.
      float verticalVelocity44 = 0.0f;         // +0x44: vertical velocity/jump vector field.
      float verticalAcceleration48 = 24.0f;    // +0x48: downward acceleration used by FUN_002262c0.
      float groundHeight4c = 0.0f;             // +0x4C: sampled ground height.
      float previousGroundHeight50 = 0.0f;     // +0x50: previous sampled ground height.
      float radius54 = 0.35f;                  // +0x54: collision radius.
      float height58 = 1.25f;                  // +0x58: collision height.
      float maxStepHeight80 = 0.75f;           // +0x80: maximum step-up height accepted by FUN_002262c0.
      std::uint16_t state60 = 0;               // +0x60: field/player movement state.
      std::uint32_t rejectTerrainMask74 = 0;   // +0x74: reject terrain when 0x78-record +0x04 overlaps this mask.
      std::uint32_t requiredTerrainMask78 = 0; // +0x78: require common footprint terrain flags to overlap this mask.
      std::uint16_t animationA0 = 1;           // +0xA0: animation id; see FUN_00256bb8.
      std::uint16_t previousSubstateA2 = 0xffff;
      std::uint16_t stateResetA4 = 999;
      std::uint16_t substateFrameA8 = 0;
      std::uint16_t idleTimer1b6 = 0;          // +0x1B6: idle fidget timer, 16-bit wrap.
      std::uint8_t motionFlags1bb = 0;
      bool running = false;
      bool pendingJumpImpulse = false;
    };

    OriginalLeadEntity entity_;

    // cGpffffb6e1 == 0x1D. Only in that camera sub-mode does FUN_00256ab0 ease
    // facing through FUN_0023a320; every other path assigns it outright.
    bool input0x1dTurnSmoothing_ = false;

    void FUN_00225bf0_set_entity_state(std::uint16_t state, std::uint16_t substate);
    void FUN_00252d88_return_to_idle_state();
    void FUN_00256bb8_update_grounded_field_state(std::uint32_t frameTicks, const OriginalPlayerFrameInput &input);
    void FUN_002534d8_update_airborne_state(std::uint32_t frameTicks, const OriginalPlayerFrameInput &input);
    void FUN_00253468_finish_landing();
    void FUN_00253488_apply_airborne_control(std::uint32_t frameTicks, const OriginalPlayerFrameInput &input);
    void FUN_00256ab0_apply_movement_impulse(float movementStep,
                                             const orphen::ported::psm2::Vec3 &cameraRelativeMove);
    OriginalTerrainQuery terrainQueryForEntity() const;
    std::optional<OriginalTerrainSample> FUN_00227390_validate_destination(float originalX,
                                                                           float originalZ,
                                                                           const OriginalTerrainSampler &terrainSampler) const;
    void FUN_002262c0_integrate_physics(std::uint32_t frameTicks,
                                        const OriginalTerrainSampler &terrainSampler,
                                        const OriginalMovementBlocker &movementBlocker);
  };

} // namespace orphen::ported::player
