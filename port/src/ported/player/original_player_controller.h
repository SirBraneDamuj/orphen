#pragma once

#include "ported/psm2/psm2_runtime.h"

#include <cstdint>
#include <functional>
#include <optional>

namespace orphen::ported::player
{

  constexpr std::uint32_t kOriginalMappedActionJump = 0x80;

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
  };

  struct OriginalPlayerSnapshot
  {
    orphen::ported::psm2::Vec3 position{};
    float facingRadians = 0.0f;
    std::uint16_t state = 0;
    std::uint16_t substate = 0;
    std::uint32_t collisionFlags = 0;
    bool grounded = false;
  };

  class OriginalPlayerController
  {
  public:
    void resetAtOrigin(const OriginalTerrainSampler &terrainSampler);
    void update(float deltaSeconds,
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
      std::uint16_t substateA0 = 1;            // +0xA0: state submode; airborne jump uses 0x0C/0x0D/0x10.
      std::uint16_t previousSubstateA2 = 0xffff;
      std::uint16_t stateResetA4 = 999;
      std::uint16_t substateFrameA8 = 0;
      std::uint8_t motionFlags1bb = 0;
      bool pendingJumpImpulse = false;
    };

    OriginalLeadEntity entity_;

    void FUN_00225bf0_set_entity_state(std::uint16_t state, std::uint16_t substate);
    void FUN_00252d88_return_to_idle_state();
    void FUN_00256bb8_update_grounded_field_state(float deltaSeconds, const OriginalPlayerFrameInput &input);
    void FUN_002534d8_update_airborne_state(float deltaSeconds, const OriginalPlayerFrameInput &input);
    void FUN_00253468_finish_landing();
    void FUN_00253488_apply_airborne_control(float deltaSeconds, const OriginalPlayerFrameInput &input);
    void FUN_00256ab0_apply_movement_impulse(float movementStep, const orphen::ported::psm2::Vec3 &cameraRelativeMove);
    OriginalTerrainQuery terrainQueryForEntity() const;
    std::optional<OriginalTerrainSample> FUN_00227390_validate_destination(float originalX,
                                                                           float originalZ,
                                                                           const OriginalTerrainSampler &terrainSampler) const;
    void FUN_002262c0_integrate_physics(float deltaSeconds,
                                        const OriginalTerrainSampler &terrainSampler,
                                        const OriginalMovementBlocker &movementBlocker);
  };

} // namespace orphen::ported::player
